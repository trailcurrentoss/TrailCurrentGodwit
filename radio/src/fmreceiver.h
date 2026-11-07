#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Broadcast FM from raw I/Q: stereo audio, RDS and a measure of signal quality.
//
// Rates are chosen so every clock in the signal lands on a whole number of
// samples. The stereo pilot is 19 kHz and RDS symbols are pilot / 16, so at
// 228 kHz the pilot is 12 samples and at 9.5 kHz a symbol is 8.
//
//   1.14 MS/s I/Q -> (mix by fs/4, channel filter, / 5) -> 228 kHz
//   228 kHz       -> discriminator                      -> multiplex
//   multiplex     -> (/ 4)  -> 57 kHz stereo audio
//   multiplex     -> (/ 24) -> 9.5 kHz RDS
namespace fm {

constexpr int kIqRate = 1140000;
constexpr int kMpxRate = kIqRate / 5;
constexpr int kAudioRate = kMpxRate / 4;
constexpr int kRdsRate = kMpxRate / 24;
// The tuner sits this far below the station, clear of its DC spike
constexpr int kOffsetHz = kIqRate / 4;

class Fir
{
public:
    Fir(int taps, double cutoffHz, double rateHz);
    void push(float x)
    {
        m_pos = m_pos == 0 ? m_n - 1 : m_pos - 1;
        m_buf[m_pos] = x;
        m_buf[m_pos + m_n] = x;
    }
    float output() const;
    void reset();

private:
    int m_n;
    int m_pos = 0;
    std::vector<float> m_taps;
    std::vector<float> m_buf;
};

// Decodes RDS (RBDS in North America) from the 57 kHz subcarrier moved to baseband
class Rds
{
public:
    void reset();
    void sample(float i, float q);

    uint16_t pi() const { return m_pi; }
    int pty() const { return m_pty; }
    const std::string &ps() const { return m_ps; }
    const std::string &text() const { return m_text; }
    // The call sign a North American station's PI encodes, or empty
    std::string callSign() const;
    uint64_t groups() const { return m_groups; }

private:
    void bit(int b);
    void block(uint32_t word, int index);
    void group();
    void publishText(int length);

    // Carrier phase and symbol timing
    float m_ai = 0, m_aq = 0;
    float m_hist[8] = {};
    int m_n = 0;
    float m_energy[8] = {};
    int m_prev = 0;

    // Block sync
    uint32_t m_word = 0;
    uint64_t m_bits = 0;
    uint64_t m_lastHit = 0;
    int m_lastIndex = -1;
    bool m_synced = false;
    int m_left = 0;
    int m_expect = 0;
    int m_bad = 0;

    // The group being collected: blocks A, B, C, D
    uint16_t m_blocks[4] = {};
    bool m_ok[4] = {};

    // Decoded
    uint16_t m_pi = 0, m_piCandidate = 0;
    int m_pty = -1;
    char m_psBuf[8] = {};
    unsigned m_psSeen = 0;
    std::string m_ps;
    char m_textBuf[64] = {};
    uint64_t m_textSeen = 0;
    int m_textAb = -1;
    int m_lastAddr = -1;
    std::string m_text;
    uint64_t m_groups = 0;
};

struct Quality
{
    float snrDb = 0;    // carrier to noise, from how steady the envelope is
    float mpxRms = 0;   // modulation; 1 is full deviation
    bool pilot = false;
    float blend = 0;    // 0 mono .. 1 full stereo
};

class Receiver
{
public:
    Receiver();

    // After a retune: forget the old station and ignore this many samples,
    // which were already on their way when the tuner moved
    void retune(int discardIqSamples);

    // Interleaved unsigned 8-bit I/Q in; interleaved s16 stereo appended to pcm
    void process(const uint8_t *iq, size_t bytes, std::vector<int16_t> &pcm);

    // Silences the audio without stopping it, while seeking
    void setSilent(bool silent) { m_silent = silent; }

    const Quality &quality() const { return m_quality; }
    const Rds &rds() const { return m_rds; }
    // Blocks of quality measured since the last retune
    int measured() const { return m_measured; }

private:
    void mpxSample(float zi, float zq);
    void endBlock();

    Fir m_chanI, m_chanQ;
    int m_mix = 0;
    int m_decIq = 0;
    int m_discard = 0;

    float m_prevI = 0, m_prevQ = 0;

    // Signal measurement
    double m_sumMag = 0, m_sumPow = 0, m_sumMpx = 0, m_sumMpx2 = 0;
    int m_blockN = 0;
    int m_measured = 0;
    Quality m_quality;

    // Pilot: a narrow bandpass, then a PLL
    float m_bp1 = 0, m_bp2 = 0, m_bpy1 = 0, m_bpy2 = 0;
    float m_theta = 0, m_freq = 0;
    float m_lockI = 0, m_lockQ = 0;

    // Audio
    Fir m_sum, m_diff;
    int m_decAudio = 0;
    float m_deL = 0, m_deR = 0;
    float m_dcInL = 0, m_dcInR = 0, m_dcOutL = 0, m_dcOutR = 0;
    float m_blendNow = 0, m_gainNow = 0, m_gainTarget = 0;
    bool m_silent = false;

    // RDS
    Fir m_rdsI, m_rdsQ;
    int m_decRds = 0;
    Rds m_rds;
};

} // namespace fm
