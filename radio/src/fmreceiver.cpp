#include "fmreceiver.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fm {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kTwoPi = float(2 * kPi);

// Quality is measured over this many multiplex samples: 80 ms
constexpr int kBlock = kMpxRate * 80 / 1000;
// North American de-emphasis
constexpr float kDeemphSeconds = 75e-6f;
// Multiplex scale: 75 kHz of deviation is 1.0
const float kDiscGain = float(kMpxRate / (2 * kPi * 75000.0));
// Pilot PLL: per sample, with a bandwidth of about 15 Hz
const float kPllOmega = float(2 * kPi * 15.0 / kMpxRate);
const float kPllAlpha = 2 * 0.707f * kPllOmega;
const float kPllBeta = kPllOmega * kPllOmega;
const float kPllCenter = float(2 * kPi * 19000.0 / kMpxRate);
const float kPllLimit = float(2 * kPi * 50.0 / kMpxRate);
// A pilot at the standard 9% of deviation reads 0.045 here
constexpr float kPilotNominal = 0.045f;
constexpr float kPilotOn = 0.018f;
constexpr float kPilotOff = 0.010f;
// Follows the lock detector over about 50 ms
const float kLockCoef = 1.0f / (kMpxRate * 0.05f);

// RDS block check: the generator polynomial and each block's offset word
constexpr uint32_t kRdsPoly = 0x5B9;
constexpr uint16_t kOffA = 0x0FC, kOffB = 0x198, kOffC = 0x168, kOffCp = 0x350, kOffD = 0x1B4;

uint32_t syndrome(uint32_t word)
{
    uint32_t rem = word & 0x3FFFFFF;
    for (int i = 25; i >= 10; --i)
        if ((rem >> i) & 1)
            rem ^= kRdsPoly << (i - 10);
    return rem & 0x3FF;
}

// Which block of a group this word is (C' counts as C), or -1
int blockIndex(uint32_t syn)
{
    switch (syn) {
    case kOffA: return 0;
    case kOffB: return 1;
    case kOffC:
    case kOffCp: return 2;
    case kOffD: return 3;
    default: return -1;
    }
}

char rdsChar(unsigned c)
{
    return c >= 0x20 && c < 0x7F ? char(c) : ' ';
}

std::string trimmed(const char *s, int n)
{
    int start = 0;
    while (start < n && s[start] == ' ')
        ++start;
    while (n > start && s[n - 1] == ' ')
        --n;
    return std::string(s + start, size_t(n - start));
}

} // namespace

// ---------------------------------------------------------------- Fir

Fir::Fir(int taps, double cutoffHz, double rateHz)
    : m_n(taps), m_taps(size_t(taps)), m_buf(size_t(taps) * 2, 0.f)
{
    // Windowed sinc, Hamming; symmetric, so the delay line can run either way
    double sum = 0;
    const double fc = cutoffHz / rateHz;
    std::vector<double> h(static_cast<size_t>(taps));
    for (int i = 0; i < taps; ++i) {
        const double x = i - (taps - 1) / 2.0;
        const double sinc = std::abs(x) < 1e-9 ? 2 * fc : std::sin(2 * kPi * fc * x) / (kPi * x);
        h[size_t(i)] = sinc * (0.54 - 0.46 * std::cos(2 * kPi * i / (taps - 1)));
        sum += h[size_t(i)];
    }
    for (int i = 0; i < taps; ++i)
        m_taps[size_t(i)] = float(h[size_t(i)] / sum);
}

float Fir::output() const
{
    const float *t = m_taps.data();
    const float *b = m_buf.data() + m_pos;
    float s = 0;
    for (int i = 0; i < m_n; ++i)
        s += t[i] * b[i];
    return s;
}

void Fir::reset()
{
    std::fill(m_buf.begin(), m_buf.end(), 0.f);
    m_pos = 0;
}

// ---------------------------------------------------------------- Rds

void Rds::reset()
{
    *this = Rds();
}

// One complex sample at 8 per symbol. RDS is differentially coded biphase
// BPSK, locked to the pilot, so the carrier's phase holds still: squaring
// removes the data and gives the phase directly, with no loop to settle. The
// pi ambiguity left over does not matter to differential coding.
void Rds::sample(float i, float q)
{
    const float leak = 1.0f - 1.0f / (kRdsRate * 0.2f);
    m_ai = leak * m_ai + (i * i - q * q) * (1 - leak);
    m_aq = leak * m_aq + (2 * i * q) * (1 - leak);
    const float phase = 0.5f * std::atan2(m_aq, m_ai);
    const float r = i * std::cos(phase) + q * std::sin(phase);

    // Correlate the last symbol's worth with the biphase shape, + then -
    const int slot = m_n & 7;
    m_hist[slot] = r;
    float mf = 0;
    for (int k = 0; k < 4; ++k)
        mf += m_hist[(slot - k) & 7] * -1.0f + m_hist[(slot - k - 4) & 7];
    // Symbol timing: whichever of the 8 phases carries the most energy
    m_energy[slot] = 0.998f * m_energy[slot] + std::abs(mf);
    int best = 0;
    for (int k = 1; k < 8; ++k)
        if (m_energy[k] > m_energy[best])
            best = k;
    if (slot == best) {
        const int sym = mf > 0 ? 1 : 0;
        bit(sym ^ m_prev);
        m_prev = sym;
    }
    ++m_n;
}

void Rds::bit(int b)
{
    m_word = ((m_word << 1) | uint32_t(b)) & 0x3FFFFFF;
    ++m_bits;

    if (!m_synced) {
        // Two blocks the right distance apart and in the right order
        const int index = blockIndex(syndrome(m_word));
        if (index < 0)
            return;
        const uint64_t dist = m_bits - m_lastHit;
        if (m_lastIndex >= 0 && dist % 26 == 0 && dist <= 26 * 8
            && (m_lastIndex + int(dist / 26)) % 4 == index) {
            m_synced = true;
            m_bad = 0;
            m_left = 26;
            m_expect = (index + 1) % 4;
            std::memset(m_ok, 0, sizeof m_ok);
            if (index == 3)
                std::memset(m_ok, 0, sizeof m_ok);
        }
        m_lastHit = m_bits;
        m_lastIndex = index;
        return;
    }

    if (--m_left > 0)
        return;
    m_left = 26;
    const int index = m_expect;
    m_expect = (m_expect + 1) % 4;
    const uint32_t syn = syndrome(m_word);
    const bool good = blockIndex(syn) == index;
    m_ok[index] = good;
    if (good) {
        block(m_word, index);
        m_bad = std::max(0, m_bad - 1);
    } else if (++m_bad > 40) {
        m_synced = false;
        m_lastIndex = -1;
    }
    if (index == 3) {
        group();
        std::memset(m_ok, 0, sizeof m_ok);
    }
}

void Rds::block(uint32_t word, int index)
{
    m_blocks[index] = uint16_t(word >> 10);
    if (index == 0) {
        const uint16_t pi = m_blocks[0];
        // Twice running, as a block can pass its check by chance
        if (pi == m_piCandidate)
            m_pi = pi;
        m_piCandidate = pi;
    }
}

void Rds::group()
{
    if (!m_ok[1])
        return;
    ++m_groups;
    const uint16_t b = m_blocks[1];
    const int type = b >> 12;
    const bool versionB = (b >> 11) & 1;
    m_pty = (b >> 5) & 31;

    // The station's name, two characters a group
    if (type == 0 && m_ok[3]) {
        const int seg = b & 3;
        m_psBuf[seg * 2] = rdsChar(m_blocks[3] >> 8);
        m_psBuf[seg * 2 + 1] = rdsChar(m_blocks[3] & 0xFF);
        m_psSeen |= 1u << seg;
        // A whole name at a time: stations that scroll text through it would
        // otherwise show a mix of the old and the new
        if (m_psSeen == 0xF) {
            m_ps = trimmed(m_psBuf, 8);
            m_psSeen = 0;
        }
    }

    // Radio text: what is on, four characters a group (two in version B)
    if (type == 2) {
        const int ab = (b >> 4) & 1;
        if (ab != m_textAb) {
            std::memset(m_textBuf, ' ', sizeof m_textBuf);
            m_textSeen = 0;
            m_textAb = ab;
            m_lastAddr = -1;
        }
        const int addr = b & 15;
        const int width = versionB ? 2 : 4;
        const int pos = addr * width;
        char chars[4];
        int have = 0;
        if (!versionB && m_ok[2]) {
            chars[0] = char(m_blocks[2] >> 8);
            chars[1] = char(m_blocks[2] & 0xFF);
            have = 2;
        }
        if (m_ok[3] && (versionB || have == 2)) {
            chars[have++] = char(m_blocks[3] >> 8);
            chars[have++] = char(m_blocks[3] & 0xFF);
        }
        if (have != width)
            return;
        // A carriage return ends the text early
        int end = -1;
        for (int k = 0; k < width; ++k) {
            const auto c = static_cast<unsigned char>(chars[k]);
            if (c == 0x0D) {
                end = pos + k;
                break;
            }
            m_textBuf[pos + k] = rdsChar(c);
            m_textSeen |= uint64_t(1) << (pos + k);
        }
        const bool wrapped = m_lastAddr >= 0 && addr <= m_lastAddr;
        const int lastEnd = (m_lastAddr + 1) * width;
        m_lastAddr = addr;
        if (end >= 0)
            publishText(end);
        else if (wrapped)
            publishText(lastEnd);
    }
}

void Rds::publishText(int length)
{
    length = std::min(length, 64);
    // Only once every character up to the end has arrived
    for (int k = 0; k < length; ++k)
        if (!((m_textSeen >> k) & 1))
            return;
    if (length > 0)
        m_text = trimmed(m_textBuf, length);
}

std::string Rds::callSign() const
{
    char first;
    int n;
    if (m_pi >= 0x1000 && m_pi <= 0x54A7) {
        first = 'K';
        n = m_pi - 0x1000;
    } else if (m_pi >= 0x54A8 && m_pi <= 0x994F) {
        first = 'W';
        n = m_pi - 0x54A8;
    } else {
        return {};
    }
    const char out[4] = { first, char('A' + n / 676), char('A' + (n % 676) / 26), char('A' + n % 26) };
    return std::string(out, 4);
}

// ---------------------------------------------------------------- Receiver

Receiver::Receiver()
    // Nearest neighbour 200 kHz away folds to 28 kHz at 228 kHz, so the stopband
    // has to start by 128 kHz
    : m_chanI(135, 114000, kIqRate)
    , m_chanQ(135, 114000, kIqRate)
    // Flat to 15 kHz, and well down by the 19 kHz pilot
    , m_sum(189, 17000, kMpxRate)
    , m_diff(189, 17000, kMpxRate)
    , m_rdsI(161, 2600, kMpxRate)
    , m_rdsQ(161, 2600, kMpxRate)
{
}

void Receiver::retune(int discardIqSamples)
{
    m_discard = discardIqSamples;
    m_chanI.reset();
    m_chanQ.reset();
    m_sum.reset();
    m_diff.reset();
    m_rdsI.reset();
    m_rdsQ.reset();
    m_rds.reset();
    m_decIq = m_decAudio = m_decRds = 0;
    m_prevI = m_prevQ = 0;
    m_sumMag = m_sumPow = m_sumMpx = m_sumMpx2 = 0;
    m_blockN = 0;
    m_measured = 0;
    m_quality = Quality();
    m_bp1 = m_bp2 = m_bpy1 = m_bpy2 = 0;
    m_theta = 0;
    m_freq = 0;
    m_lockI = m_lockQ = 0;
    m_deL = m_deR = 0;
    m_dcInL = m_dcInR = m_dcOutL = m_dcOutR = 0;
    m_blendNow = 0;
    m_gainNow = 0;
    m_gainTarget = 0;
}

void Receiver::process(const uint8_t *iq, size_t bytes, std::vector<int16_t> &pcm)
{
    for (size_t k = 0; k + 1 < bytes; k += 2) {
        float i = (float(iq[k]) - 127.5f) / 127.5f;
        float q = (float(iq[k + 1]) - 127.5f) / 127.5f;

        // Multiply by e^(-j pi n / 2), bringing the station at +fs/4 to zero
        float mi, mq;
        switch (m_mix) {
        case 0: mi = i;  mq = q;  break;
        case 1: mi = q;  mq = -i; break;
        case 2: mi = -i; mq = -q; break;
        default: mi = -q; mq = i; break;
        }
        m_mix = (m_mix + 1) & 3;

        if (m_discard > 0) {
            --m_discard;
            // The audio keeps flowing, silent, so the player does not run dry
            if (++m_decIq == 20) {
                m_decIq = 0;
                pcm.push_back(0);
                pcm.push_back(0);
            }
            if (m_discard == 0)
                m_decIq = 0;
            continue;
        }

        m_chanI.push(mi);
        m_chanQ.push(mq);
        if (++m_decIq < 5)
            continue;
        m_decIq = 0;
        mpxSample(m_chanI.output(), m_chanQ.output());

        if (m_decAudio != 0)
            continue;
        const float s = m_sum.output();
        const float d = m_diff.output() * m_blendNow;
        float l = s + d, r = s - d;

        // De-emphasis
        static const float a = std::exp(-1.0f / (kAudioRate * kDeemphSeconds));
        m_deL = a * m_deL + (1 - a) * l;
        m_deR = a * m_deR + (1 - a) * r;

        // Any frequency error between tuner and station comes out as DC
        const float outL = m_deL - m_dcInL + 0.999f * m_dcOutL;
        const float outR = m_deR - m_dcInR + 0.999f * m_dcOutR;
        m_dcInL = m_deL;
        m_dcInR = m_deR;
        m_dcOutL = outL;
        m_dcOutR = outR;

        // Fades in and out over about 40 ms
        m_gainNow += (m_gainTarget - m_gainNow) * 0.0005f;
        const float g = m_silent ? 0.f : m_gainNow;
        pcm.push_back(int16_t(std::clamp(outL * g * 29000.f, -32767.f, 32767.f)));
        pcm.push_back(int16_t(std::clamp(outR * g * 29000.f, -32767.f, 32767.f)));
    }
}

void Receiver::mpxSample(float zi, float zq)
{
    // Envelope: an FM carrier's is steady, noise's is not
    const float pow = zi * zi + zq * zq;
    m_sumPow += pow;
    m_sumMag += std::sqrt(pow);

    // Frequency, as the phase step from the sample before
    const float re = zi * m_prevI + zq * m_prevQ;
    const float im = zq * m_prevI - zi * m_prevQ;
    m_prevI = zi;
    m_prevQ = zq;
    const float mpx = std::atan2(im, re) * kDiscGain;
    m_sumMpx += mpx;
    m_sumMpx2 += mpx * mpx;

    // Narrow bandpass at the pilot: 200 Hz wide, around 19 kHz
    static const double w0 = 2 * kPi * 19000.0 / kMpxRate;
    static const double al = std::sin(w0) / (2 * 95.0);
    static const float b0 = float(al / (1 + al));
    static const float a1 = float(-2 * std::cos(w0) / (1 + al));
    static const float a2 = float((1 - al) / (1 + al));
    const float bp = b0 * (mpx - m_bp2) - a1 * m_bpy1 - a2 * m_bpy2;
    m_bp2 = m_bp1;
    m_bp1 = mpx;
    m_bpy2 = m_bpy1;
    m_bpy1 = bp;

    // Pilot is A sin(phi); the PLL holds theta on phi
    const float sn = std::sin(m_theta), cs = std::cos(m_theta);
    const float err = bp * cs / kPilotNominal;
    m_freq = std::clamp(m_freq + kPllBeta * err, -kPllLimit, kPllLimit);
    m_theta += kPllCenter + m_freq + kPllAlpha * err;
    if (m_theta >= kTwoPi)
        m_theta -= kTwoPi;
    else if (m_theta < 0)
        m_theta += kTwoPi;
    m_lockI += (bp * sn - m_lockI) * kLockCoef;
    m_lockQ += (bp * cs - m_lockQ) * kLockCoef;

    // 38 and 57 kHz from the pilot's phase
    const float s2 = 2 * sn * cs, c2 = cs * cs - sn * sn;
    const float s3 = s2 * cs + c2 * sn, c3 = c2 * cs - s2 * sn;

    m_sum.push(mpx);
    m_diff.push(mpx * 2 * s2);
    m_rdsI.push(mpx * 2 * c3);
    m_rdsQ.push(mpx * -2 * s3);

    m_decAudio = (m_decAudio + 1) & 3;
    if (++m_decRds == 24) {
        m_decRds = 0;
        m_rds.sample(m_rdsI.output(), m_rdsQ.output());
    }

    if (++m_blockN >= kBlock)
        endBlock();
}

void Receiver::endBlock()
{
    const double n = m_blockN;
    const double mean = m_sumMag / n;
    const double var = std::max(0.0, m_sumPow / n - mean * mean);
    const double cv = mean > 1e-6 ? std::sqrt(var) / mean : 1.0;
    // A carrier with noise on it: cv = 1 / sqrt(2 SNR). Noise alone reads about
    // 3 dB, as its envelope has a cv of 0.52.
    const double snr = cv > 1e-4 ? 20 * std::log10(1 / (cv * std::sqrt(2.0))) : 60.0;
    m_quality.snrDb = float(std::clamp(snr, 0.0, 60.0));

    const double mpxMean = m_sumMpx / n;
    m_quality.mpxRms = float(std::sqrt(std::max(0.0, m_sumMpx2 / n - mpxMean * mpxMean)));

    if (!m_quality.pilot && m_lockI > kPilotOn && std::abs(m_lockQ) < 0.5f * m_lockI)
        m_quality.pilot = true;
    else if (m_quality.pilot && m_lockI < kPilotOff)
        m_quality.pilot = false;

    // As the signal weakens: blend to mono, which is far less noisy, then fade.
    // A real station's own wide peaks ripple the envelope too, so even a strong
    // one reads under 20 dB; these thresholds are set from those.
    const float blend = m_quality.pilot ? std::clamp((m_quality.snrDb - 10.f) / 6.f, 0.f, 1.f) : 0.f;
    m_blendNow += (blend - m_blendNow) * 0.5f;
    m_quality.blend = m_blendNow;
    m_gainTarget = std::clamp((m_quality.snrDb - 6.f) / 6.f, 0.03f, 1.f);

    m_sumMag = m_sumPow = m_sumMpx = m_sumMpx2 = 0;
    m_blockN = 0;
    ++m_measured;
}

} // namespace fm
