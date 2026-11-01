// godwit-wake: listens for a wake word in 16 kHz mono 16-bit audio on stdin
// and prints "wake <score>" on stdout each time it hears it.
//
// openWakeWord's three models, run with ONNX Runtime on one core:
// melspectrogram.onnx turns the audio into mel frames, embedding_model.onnx
// turns the last 76 frames into 96 features every 80 ms, and the wake word's
// own model scores the last 16 of those. The streaming follows
// openwakeword.utils.AudioFeatures, so models trained with openWakeWord's
// tools work unchanged.
//
//   pw-record --raw --rate 16000 --channels 1 --format s16 - | godwit-wake

#include <onnxruntime_cxx_api.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace {

constexpr int Chunk = 1280;          // 80 ms, one embedding
constexpr int MelContext = 480;      // three 10 ms hops carried over from the chunk before
constexpr int MelBins = 32;
constexpr int EmbedFrames = 76;
constexpr int EmbedDim = 96;

struct Options {
    std::string dir = "/usr/share/godwit/wake";
    std::string model = "hey_peregrine";
    double threshold = 0.85;
    // Chunks in a row over the threshold before it counts. Peregrine's 3 suits a
    // desk speakerphone; with a cabin microphone a clear "Hey Peregrine" often
    // holds over the threshold for only 2 of its 80 ms chunks (measured: 3 in
    // a row caught 16 of 28 attempts, 2 caught 24, at about one false wake an
    // hour of music and talk)
    int activations = 2;
    // Mean absolute sample value; quieter chunks are not scored
    double minLevel = 300;
    // Chunks after a detection that are not scored, so one phrase counts once
    int refractory = 25;
    bool verbose = false;
    // Every chunk's time, level and score on stderr, for tuning
    bool trace = false;
};

void usage()
{
    std::fprintf(stderr,
                 "usage: godwit-wake [--dir DIR] [--model NAME] [--threshold 0.85] [--activations 2]\n"
                 "                   [--min-level 300] [--verbose] [--trace]\n"
                 "Reads 16 kHz mono s16le audio on stdin, prints \"wake <score>\" per detection.\n");
}

bool parse(int argc, char **argv, Options &o)
{
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : nullptr; };
        const char *v = nullptr;
        if (a == "--verbose" || a == "--trace") {
            (a == "--trace" ? o.trace : o.verbose) = true;
            continue;
        }
        if (a == "--help" || a == "-h" || !(v = next()))
            return false;
        if (a == "--dir")
            o.dir = v;
        else if (a == "--model")
            o.model = v;
        else if (a == "--threshold")
            o.threshold = std::atof(v);
        else if (a == "--activations")
            o.activations = std::max(1, std::atoi(v));
        else if (a == "--min-level")
            o.minLevel = std::atof(v);
        else
            return false;
    }
    return true;
}

// One ONNX model with a single float input and output
class Model
{
public:
    Model(Ort::Env &env, const Ort::SessionOptions &opts, const std::string &path)
        : m_session(env, path.c_str(), opts)
    {
        Ort::AllocatorWithDefaultOptions alloc;
        m_in = m_session.GetInputNameAllocated(0, alloc).get();
        m_out = m_session.GetOutputNameAllocated(0, alloc).get();
    }

    // The input's size along axis, from the model (-1 when it is free)
    int64_t inputDim(size_t axis)
    {
        const auto shape = m_session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        return axis < shape.size() ? shape[axis] : -1;
    }

    std::vector<float> run(std::vector<float> &data, const std::vector<int64_t> &shape)
    {
        const auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in = Ort::Value::CreateTensor<float>(mem, data.data(), data.size(), shape.data(), shape.size());
        const char *inNames[] = { m_in.c_str() };
        const char *outNames[] = { m_out.c_str() };
        auto out = m_session.Run(Ort::RunOptions { nullptr }, inNames, &in, 1, outNames, 1);
        const float *p = out[0].GetTensorData<float>();
        return std::vector<float>(p, p + out[0].GetTensorTypeAndShapeInfo().GetElementCount());
    }

private:
    Ort::Session m_session;
    std::string m_in;
    std::string m_out;
};

} // namespace

int main(int argc, char **argv)
{
    Options o;
    if (!parse(argc, argv, o)) {
        usage();
        return 2;
    }

    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "godwit-wake");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetInterOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    std::vector<Model> models;
    try {
        models.emplace_back(env, opts, o.dir + "/melspectrogram.onnx");
        models.emplace_back(env, opts, o.dir + "/embedding_model.onnx");
        models.emplace_back(env, opts, o.dir + "/" + o.model + ".onnx");
    } catch (const Ort::Exception &e) {
        std::fprintf(stderr, "godwit-wake: cannot load the models in %s: %s\n", o.dir.c_str(), e.what());
        return 1;
    }
    Model &mel = models[0], &embed = models[1], &wake = models[2];
    const int features = wake.inputDim(1) > 0 ? int(wake.inputDim(1)) : 16;

    std::fprintf(stderr, "godwit-wake: listening for %s (threshold %.2f, %d in a row)\n",
                 o.model.c_str(), o.threshold, o.activations);

    // The audio fed to the mel model: the last hops of the chunk before, then this chunk
    std::vector<float> audio(MelContext + Chunk, 0.0f);
    // Mel frames, oldest first; openWakeWord starts the buffer with ones
    std::deque<std::vector<float>> frames(EmbedFrames, std::vector<float>(MelBins, 1.0f));
    // Embeddings, oldest first; scoring waits until there are enough
    std::deque<std::vector<float>> feats;

    std::vector<int16_t> pcm(Chunk);
    int consecutive = 0, quiet = 0, rest = 0;
    long chunk = 0;
    while (std::fread(pcm.data(), sizeof(int16_t), Chunk, stdin) == size_t(Chunk)) {
        std::memmove(audio.data(), audio.data() + Chunk, MelContext * sizeof(float));
        double level = 0;
        for (int i = 0; i < Chunk; ++i) {
            audio[MelContext + i] = float(pcm[i]);
            level += std::abs(int(pcm[i]));
        }
        level /= Chunk;
        ++chunk;

        try {
            std::vector<float> in = audio;
            const std::vector<float> m = mel.run(in, { 1, int64_t(in.size()) });
            for (size_t f = 0; f + MelBins <= m.size(); f += MelBins) {
                std::vector<float> row(m.begin() + f, m.begin() + f + MelBins);
                for (float &x : row)
                    x = x / 10.0f + 2.0f;
                frames.push_back(std::move(row));
                frames.pop_front();
            }

            std::vector<float> window;
            window.reserve(EmbedFrames * MelBins);
            for (const auto &row : frames)
                window.insert(window.end(), row.begin(), row.end());
            feats.push_back(embed.run(window, { 1, EmbedFrames, MelBins, 1 }));
            if (int(feats.size()) > features)
                feats.pop_front();
            if (int(feats.size()) < features)
                continue;

            if (rest > 0) {
                --rest;
                continue;
            }
            // Pauses between the words are allowed; a sustained quiet starts over
            if (level < o.minLevel) {
                if (o.trace)
                    std::fprintf(stderr, "%8.2f level %5.0f quiet\n", chunk * 0.08, level);
                if (++quiet >= 2)
                    consecutive = 0;
                continue;
            }
            quiet = 0;

            std::vector<float> x;
            x.reserve(features * EmbedDim);
            for (const auto &e : feats)
                x.insert(x.end(), e.begin(), e.end());
            const float score = wake.run(x, { 1, features, EmbedDim }).at(0);
            if (o.trace)
                std::fprintf(stderr, "%8.2f level %5.0f score %.3f\n", chunk * 0.08, level, score);
            if (score <= o.threshold) {
                if (consecutive > 0 && o.verbose)
                    std::fprintf(stderr, "godwit-wake: near miss after %d (%.2f)\n", consecutive, score);
                consecutive = 0;
                continue;
            }
            if (++consecutive >= o.activations) {
                std::printf("wake %.2f\n", score);
                std::fflush(stdout);
                consecutive = 0;
                rest = o.refractory;
            }
        } catch (const Ort::Exception &e) {
            std::fprintf(stderr, "godwit-wake: inference failed: %s\n", e.what());
            return 1;
        }
    }
    return 0;
}
