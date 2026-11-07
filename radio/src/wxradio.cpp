#include "wxradio.h"

#include <algorithm>
#include <cmath>

namespace wx {

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr int kDecimate = kIqRate / kAudioRate;
// A quality block: 100 ms
constexpr int kBlock = kAudioRate / 10;

// The transmitters' peak deviation
constexpr float kFullDeviationHz = 5000.f;
// Their speech rises 6 dB an octave across its 100 Hz to 5 kHz. No time constant is
// published; this is the usual one for two-way radio.
constexpr double kDeemphasisSeconds = 750e-6;
// De-emphasis leaves a full-deviation 1 kHz tone at about a fifth
constexpr float kVoiceGain = 3.0f;

// A carrier and noise, as fm::Receiver reads them. Noise alone reads under 3 dB, and
// speech is still partly understood at about 4.5 dB, with hiss. Better a hissy
// forecast than silence from a station the bars say is there.
constexpr float kOpenDb = 4.f;
constexpr float kCloseDb = 3.4f;
// Modulation that is speech, after de-emphasis, and not the hiss on a clean carrier
constexpr float kSpeechRms = 0.008f;
// The pauses between messages are a few seconds; a carrier with nothing on it for
// this many blocks is not a station
constexpr int kSpeechHold = 200;
// The filters ring for a moment after a channel change, which is not speech
constexpr int kSettleBlocks = 3;
// How fast the mixer chases the carrier, and how far it may go: a dongle with no
// temperature-compensated crystal can be some 30 ppm out, 5 kHz here
constexpr float kAfcRate = 0.2f;
constexpr float kAfcMaxHz = 7000.f;

// The strength measure: this much of a look every so often, averaged
constexpr int kFftSize = 1024;
constexpr int kFftLog2 = 10;
constexpr int kFftEvery = kIqRate / 10;
constexpr int kFftAverage = 8;
constexpr double kBinHz = double(kIqRate) / kFftSize;
// A station's speech is within this of its channel
constexpr double kStationHalfHz = 7000;
// The quiet between channels, either side of the midpoint
constexpr double kGuardHalfHz = 1500;

void fft(std::vector<float> &re, std::vector<float> &im)
{
    const int n = int(re.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[size_t(i)], re[size_t(j)]);
            std::swap(im[size_t(i)], im[size_t(j)]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double a = -2 * kPi / len;
        const float wr = float(std::cos(a)), wi = float(std::sin(a));
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k) {
                const size_t p = size_t(i + k), q = size_t(i + k + len / 2);
                const float tr = re[q] * cr - im[q] * ci;
                const float ti = re[q] * ci + im[q] * cr;
                re[q] = re[p] - tr;
                im[q] = im[p] - ti;
                re[p] += tr;
                im[p] += ti;
                const float next = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = next;
            }
        }
    }
}

int binOf(double offsetHz)
{
    return (int(std::lround(offsetHz / kBinHz)) % kFftSize + kFftSize) % kFftSize;
}

const std::vector<float> &hann()
{
    static const std::vector<float> w = [] {
        std::vector<float> h(kFftSize);
        for (int i = 0; i < kFftSize; ++i)
            h[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * kPi * i / kFftSize));
        return h;
    }();
    return w;
}

} // namespace

int channelIndex(int hz)
{
    for (int i = 0; i < kChannels; ++i)
        if (kChannelHz[size_t(i)] == hz)
            return i;
    return -1;
}

Receiver::Receiver()
    // Whatever lands within 15 kHz of the channel after / 6 is taken out next, so
    // this only has to stop what would fold onto that: 33 kHz and up
    : m_firstI(45, 21000, kIqRate)
    , m_firstQ(45, 21000, kIqRate)
    // 16 kHz wide, and the next channel's edge is 17 kHz out
    , m_chanI(31, 11500, kAudioRate)
    , m_chanQ(31, 11500, kAudioRate)
    , m_voice(41, 5500, kAudioRate)
    , m_fftI(kFftSize)
    , m_fftQ(kFftSize)
    , m_power(kFftSize, 0.f)
{
    setChannel(0);
}

void Receiver::setChannel(int index)
{
    m_channel = std::clamp(index, 0, kChannels - 1);
    setMixer();
    m_sumMag = m_sumPow = m_sumDisc = m_sumVoice2 = 0;
    m_blockN = 0;
    m_snrDb = 0;
    m_modulation = 0;
    m_carrier = false;
    m_open = false;
    m_sinceSpeech = kSpeechHold;
    m_settle = kSettleBlocks;
}

void Receiver::setMixer()
{
    const double hz = double(kChannelHz[size_t(m_channel)] - kTunerHz) + double(m_afcHz);
    const double w = -2 * kPi * hz / kIqRate;
    m_stepI = float(std::cos(w));
    m_stepQ = float(std::sin(w));
}

void Receiver::process(const uint8_t *iq, size_t bytes, std::vector<int16_t> &pcm)
{
    const std::vector<float> &window = hann();
    for (size_t n = 0; n + 1 < bytes; n += 2) {
        const float i = (float(iq[n]) - 127.5f) / 127.5f;
        const float q = (float(iq[n + 1]) - 127.5f) / 127.5f;

        if (m_fftFill < kFftSize) {
            m_fftI[size_t(m_fftFill)] = i * window[size_t(m_fftFill)];
            m_fftQ[size_t(m_fftFill)] = q * window[size_t(m_fftFill)];
            if (++m_fftFill == kFftSize) {
                measureLevels();
                m_fftWait = kFftEvery - kFftSize;
            }
        } else if (--m_fftWait <= 0) {
            m_fftFill = 0;
        }

        const float mi = i * m_rotI - q * m_rotQ;
        const float mq = i * m_rotQ + q * m_rotI;
        const float nextI = m_rotI * m_stepI - m_rotQ * m_stepQ;
        m_rotQ = m_rotI * m_stepQ + m_rotQ * m_stepI;
        m_rotI = nextI;
        if (++m_rotAge >= 4096) {
            m_rotAge = 0;
            const float mag = std::sqrt(m_rotI * m_rotI + m_rotQ * m_rotQ);
            m_rotI /= mag;
            m_rotQ /= mag;
        }

        m_firstI.push(mi);
        m_firstQ.push(mq);
        if (++m_dec < kDecimate)
            continue;
        m_dec = 0;

        m_chanI.push(m_firstI.output());
        m_chanQ.push(m_firstQ.output());
        const float zi = m_chanI.output();
        const float zq = m_chanQ.output();

        const float mag2 = zi * zi + zq * zq;
        const float disc = std::atan2(zq * m_prevI - zi * m_prevQ, zi * m_prevI + zq * m_prevQ)
            * float(kAudioRate / (2 * kPi)) / kFullDeviationHz;
        m_prevI = zi;
        m_prevQ = zq;

        m_sumMag += std::sqrt(mag2);
        m_sumPow += mag2;
        m_sumDisc += disc;

        static const float de = float(1 - std::exp(-1.0 / (kAudioRate * kDeemphasisSeconds)));
        m_de += (disc - m_de) * de;
        m_voice.push(m_de);
        const float x = m_voice.output();
        // What is left of a tuner slightly off frequency, and any hum
        const float out = x - m_dcIn + 0.9895f * m_dcOut;
        m_dcIn = x;
        m_dcOut = out;
        m_sumVoice2 += out * out;

        // A short fade, so the squelch does not click
        m_gain += ((m_open ? 1.f : 0.f) - m_gain) * 0.002f;
        const float s = std::clamp(out * kVoiceGain * m_gain, -1.f, 1.f);
        pcm.push_back(int16_t(std::lround(s * 32000.f)));

        if (++m_blockN >= kBlock)
            endBlock();
    }
}

void Receiver::endBlock()
{
    const double n = m_blockN;
    const double mean = m_sumMag / n;
    const double var = std::max(0.0, m_sumPow / n - mean * mean);
    const double cv = mean > 1e-6 ? std::sqrt(var) / mean : 1.0;
    const double snr = cv > 1e-4 ? 20 * std::log10(1 / (cv * std::sqrt(2.0))) : 60.0;
    m_snrDb = float(std::clamp(snr, 0.0, 60.0));

    const double discMean = m_sumDisc / n;
    m_modulation = float(std::sqrt(m_sumVoice2 / n));

    if (m_snrDb >= kOpenDb)
        m_carrier = true;
    else if (m_snrDb < kCloseDb)
        m_carrier = false;

    if (m_settle > 0)
        --m_settle;
    else if (m_carrier && m_modulation >= kSpeechRms)
        m_sinceSpeech = 0;
    else if (m_sinceSpeech < kSpeechHold)
        ++m_sinceSpeech;
    m_open = m_carrier && m_sinceSpeech < kSpeechHold;

    // Follows only a carrier: noise would walk it away
    if (m_carrier) {
        m_afcHz = std::clamp(m_afcHz + float(discMean) * kFullDeviationHz * kAfcRate, -kAfcMaxHz, kAfcMaxHz);
        setMixer();
    }

    m_sumMag = m_sumPow = m_sumDisc = m_sumVoice2 = 0;
    m_blockN = 0;
}

void Receiver::measureLevels()
{
    fft(m_fftI, m_fftQ);
    for (int i = 0; i < kFftSize; ++i)
        m_power[size_t(i)] += m_fftI[size_t(i)] * m_fftI[size_t(i)] + m_fftQ[size_t(i)] * m_fftQ[size_t(i)];
    if (++m_fftCount < kFftAverage)
        return;
    m_fftCount = 0;

    // The quiet between channels, and beyond the two at the ends. The tuner's DC
    // spike is at one of the midpoints, and the median takes care of that.
    std::vector<float> guard;
    for (int c = 0; c <= kChannels; ++c) {
        const double mid = double(kChannelHz[0] - kTunerHz) + (c - 0.5) * 25000;
        for (int b = binOf(mid - kGuardHalfHz), k = 0; k <= int(2 * kGuardHalfHz / kBinHz); ++k)
            guard.push_back(m_power[size_t((b + k) % kFftSize)]);
    }
    std::nth_element(guard.begin(), guard.begin() + long(guard.size() / 2), guard.end());
    const float noise = std::max(guard[guard.size() / 2], 1e-9f);

    for (int c = 0; c < kChannels; ++c) {
        const double at = double(kChannelHz[size_t(c)] - kTunerHz);
        const int count = int(2 * kStationHalfHz / kBinHz) + 1;
        const int first = binOf(at - kStationHalfHz);
        double sum = 0;
        for (int k = 0; k < count; ++k)
            sum += double(m_power[size_t((first + k) % kFftSize)]);
        m_levelsDb[size_t(c)] = std::max(0.f, float(10 * std::log10(sum / count / double(noise))));
    }
    std::fill(m_power.begin(), m_power.end(), 0.f);
    ++m_levelBlocks;
}

} // namespace wx
