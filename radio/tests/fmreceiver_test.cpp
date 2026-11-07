// Builds a complete FM broadcast from scratch (stereo, pilot and RDS, frequency
// modulated onto I/Q at the tuner's rate) and checks what the receiver makes
// of it. Also checks that noise alone is not taken for a station.

#include "fmreceiver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
int failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++failures;
}

// ---- The RDS encoder: blocks with their check words, differential, biphase

uint32_t blockWord(uint16_t info, uint16_t offset)
{
    uint32_t rem = uint32_t(info) << 10;
    for (int i = 25; i >= 10; --i)
        if ((rem >> i) & 1)
            rem ^= 0x5B9u << (i - 10);
    return (uint32_t(info) << 10) | ((rem & 0x3FF) ^ offset);
}

std::vector<int> rdsBits(uint16_t pi, const char *ps, const char *text, int pty)
{
    std::vector<int> bits;
    auto group = [&](uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
        const uint32_t words[4] = { blockWord(a, 0x0FC), blockWord(b, 0x198), blockWord(c, 0x168), blockWord(d, 0x1B4) };
        for (uint32_t w : words)
            for (int i = 25; i >= 0; --i)
                bits.push_back((w >> i) & 1);
    };
    const uint16_t common = uint16_t(pty << 5);
    for (int round = 0; round < 12; ++round) {
        for (int seg = 0; seg < 4; ++seg)
            group(pi, uint16_t(common | seg), 0xE0CD,
                  uint16_t((uint8_t(ps[seg * 2]) << 8) | uint8_t(ps[seg * 2 + 1])));
        const int len = int(std::char_traits<char>::length(text));
        for (int seg = 0; seg * 4 < len; ++seg) {
            char c[4] = { ' ', ' ', ' ', ' ' };
            for (int k = 0; k < 4 && seg * 4 + k < len; ++k)
                c[k] = text[seg * 4 + k];
            group(pi, uint16_t((2u << 12) | common | seg),
                  uint16_t((uint8_t(c[0]) << 8) | uint8_t(c[1])),
                  uint16_t((uint8_t(c[2]) << 8) | uint8_t(c[3])));
        }
    }
    return bits;
}

// ---- The transmitter

struct Broadcast
{
    double seconds;
    double left = 1000, right = 3000;   // tone frequencies; 0 for silence
    bool stereo = true;
    bool rds = true;
    double noise = 0.004;
};

std::vector<uint8_t> transmit(const Broadcast &b, const std::vector<int> &bits)
{
    const int rate = fm::kIqRate;
    const size_t n = size_t(b.seconds * rate);
    std::vector<uint8_t> iq(n * 2);
    std::mt19937 rng(1234);
    std::normal_distribution<float> gauss(0.f, 1.f);

    double phase = 0;
    int prev = 0;
    int sym = -1;
    double level = 0;
    for (size_t k = 0; k < n; ++k) {
        const double t = double(k) / rate;
        const double w = 2 * kPi * 19000.0 * t;
        const double l = b.left > 0 ? 0.8 * std::sin(2 * kPi * b.left * t) : 0;
        const double r = b.right > 0 ? 0.8 * std::sin(2 * kPi * b.right * t) : 0;
        double mpx = 0.9 * (l + r) / 2;
        if (b.stereo) {
            mpx += 0.09 * std::sin(w);
            mpx += 0.9 * (l - r) / 2 * std::sin(2 * w);
        }
        if (b.rds && !bits.empty()) {
            // A symbol is 16 pilot cycles; biphase flips halfway through it
            const double cycles = 19000.0 * t / 16;
            const int s = int(cycles);
            if (s != sym) {
                sym = s;
                prev ^= bits[size_t(s) % bits.size()];
                level = prev ? 1 : -1;
            }
            const double half = cycles - s < 0.5 ? level : -level;
            // In quadrature with the pilot's third harmonic
            mpx += 0.04 * half * std::cos(3 * w);
        }
        phase += 2 * kPi * 75000.0 * mpx / rate;
        // The station sits at +fs/4 from the tuner
        const double carrier = 2 * kPi * fm::kOffsetHz * t + phase;
        const double i = 0.4 * std::cos(carrier) + b.noise * gauss(rng);
        const double q = 0.4 * std::sin(carrier) + b.noise * gauss(rng);
        iq[k * 2] = uint8_t(std::lround(std::clamp(i * 127.5 + 127.5, 0.0, 255.0)));
        iq[k * 2 + 1] = uint8_t(std::lround(std::clamp(q * 127.5 + 127.5, 0.0, 255.0)));
    }
    return iq;
}

double toneLevel(const std::vector<int16_t> &pcm, int channel, double hz, size_t skipFrames)
{
    double re = 0, im = 0;
    size_t count = 0;
    for (size_t f = skipFrames; f * 2 + 1 < pcm.size(); ++f, ++count) {
        const double a = 2 * kPi * hz * double(f) / fm::kAudioRate;
        const double x = pcm[f * 2 + size_t(channel)] / 32768.0;
        re += x * std::cos(a);
        im += x * std::sin(a);
    }
    return count ? 2 * std::sqrt(re * re + im * im) / double(count) : 0;
}

void run(fm::Receiver &rx, const std::vector<uint8_t> &iq, std::vector<int16_t> &pcm)
{
    for (size_t k = 0; k < iq.size(); k += 16384)
        rx.process(iq.data() + k, std::min<size_t>(16384, iq.size() - k), pcm);
}

} // namespace

int main()
{
    // 0x3AAB is KQED under the RBDS call sign rule
    const auto bits = rdsBits(0x3AAB, "TEST FM ", "Now playing a test tone\r", 5);

    {
        Broadcast b;
        b.seconds = 8;
        const auto iq = transmit(b, bits);
        fm::Receiver rx;
        std::vector<int16_t> pcm;
        run(rx, iq, pcm);

        const size_t skip = fm::kAudioRate;
        const double ll = toneLevel(pcm, 0, 1000, skip), lr = toneLevel(pcm, 1, 1000, skip);
        const double rr = toneLevel(pcm, 1, 3000, skip), rl = toneLevel(pcm, 0, 3000, skip);
        const double sep1 = 20 * std::log10(ll / std::max(lr, 1e-9));
        const double sep2 = 20 * std::log10(rr / std::max(rl, 1e-9));
        std::printf("levels L %.3f R %.3f; separation %.1f / %.1f dB; snr %.1f dB; rms %.3f; groups %llu\n",
                    ll, rr, sep1, sep2, double(rx.quality().snrDb), double(rx.quality().mpxRms),
                    static_cast<unsigned long long>(rx.rds().groups()));

        check(rx.quality().pilot, "finds the pilot");
        check(rx.quality().blend > 0.9f, "goes to full stereo on a clean signal");
        check(ll > 0.3 && rr > 0.2, "audio comes out at a sensible level");
        check(sep1 > 30 && sep2 > 30, "left and right stay apart, by over 30 dB");
        check(rx.quality().snrDb > 25, "reads a clean signal as strong");
        check(rx.rds().groups() > 50, "decodes RDS groups");
        check(rx.rds().callSign() == "KQED", "call sign from the PI code: " + rx.rds().callSign());
        check(rx.rds().ps() == "TEST FM", "station name: '" + rx.rds().ps() + "'");
        check(rx.rds().pty() == 5, "programme type");
        check(rx.rds().text() == "Now playing a test tone", "radio text: '" + rx.rds().text() + "'");
    }
    {
        // A mono station: no pilot, no RDS
        Broadcast b;
        b.seconds = 2;
        b.stereo = false;
        b.rds = false;
        b.left = b.right = 1000;
        fm::Receiver rx;
        std::vector<int16_t> pcm;
        run(rx, transmit(b, {}), pcm);
        check(!rx.quality().pilot, "a mono station shows no pilot");
        check(rx.quality().blend == 0.f, "and stays mono");
        check(toneLevel(pcm, 0, 1000, fm::kAudioRate) > 0.3, "and is still heard");
    }
    {
        // An empty channel: only noise
        std::mt19937 rng(99);
        std::normal_distribution<float> gauss(0.f, 30.f);
        std::vector<uint8_t> iq(size_t(fm::kIqRate) * 2);
        for (auto &x : iq)
            x = uint8_t(std::clamp(127.5f + gauss(rng), 0.f, 255.f));
        fm::Receiver rx;
        std::vector<int16_t> pcm;
        run(rx, iq, pcm);
        std::printf("noise: snr %.1f dB, rms %.3f\n", double(rx.quality().snrDb), double(rx.quality().mpxRms));
        check(rx.quality().snrDb < 8, "noise reads as no signal");
        check(!rx.quality().pilot, "noise shows no pilot");
        double peak = 0;
        for (size_t k = pcm.size() / 2; k < pcm.size(); ++k)
            peak = std::max(peak, std::abs(double(pcm[k])));
        check(peak < 32768 * 0.2, "noise is faded right down");
    }
    {
        // Retuning: the receiver has to start clean after the discard
        Broadcast b;
        b.seconds = 1;
        const auto iq = transmit(b, bits);
        fm::Receiver rx;
        std::vector<int16_t> pcm;
        run(rx, iq, pcm);
        rx.retune(fm::kIqRate / 10);
        check(rx.measured() == 0 && rx.rds().ps().empty(), "retuning forgets the old station");
        pcm.clear();
        rx.process(iq.data(), 40000, pcm);
        check(pcm.size() == 2000 && rx.measured() == 0, "the discard stays silent and keeps audio flowing");
        bool silent = true;
        for (int16_t v : pcm)
            silent = silent && v == 0;
        check(silent, "with zeros");
    }

    std::printf("%s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
