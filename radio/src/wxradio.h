#pragma once

#include "fmreceiver.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// NOAA Weather Radio from raw I/Q: one channel's speech, and how strong all seven
// are, at once.
//
// The channels sit 25 kHz apart in 150 kHz, so one fixed tuning hears them all and
// changing channel only moves a mixer. The tuner sits between two channels, which
// keeps its DC spike out of both.
//
//   288 kS/s I/Q -> mix the channel to zero, / 6 -> 48 kHz
//   48 kHz       -> channel filter -> discriminator -> de-emphasis -> speech
//   288 kS/s I/Q -> a short FFT now and then -> the strength of every channel
namespace wx {

constexpr int kIqRate = 288000;
constexpr int kAudioRate = kIqRate / 6;
constexpr int kTunerHz = 162462500;
constexpr int kChannels = 7;
constexpr std::array<int, kChannels> kChannelHz = {
    162400000, 162425000, 162450000, 162475000, 162500000, 162525000, 162550000,
};

// Which of the seven this is, or -1. Nothing else can be listened to.
int channelIndex(int hz);

class Receiver
{
public:
    Receiver();

    void setChannel(int index);
    int channel() const { return m_channel; }

    // Interleaved unsigned 8-bit I/Q in; s16 mono appended to pcm
    void process(const uint8_t *iq, size_t bytes, std::vector<int16_t> &pcm);

    // The channel being listened to: carrier to noise, from how steady the
    // envelope is (as fm::Receiver has it), and how much speech there is, as an rms
    // after de-emphasis
    float snrDb() const { return m_snrDb; }
    float modulation() const { return m_modulation; }
    // A station is on it: a carrier with speech on it lately. A spur from the board
    // is steady too but carries nothing, and noise is not steady.
    bool receiving() const { return m_open; }

    // How far each channel stands above the noise between channels, in dB. New
    // values about every 0.8 s; levelBlocks counts them.
    const std::array<float, kChannels> &levelsDb() const { return m_levelsDb; }
    int levelBlocks() const { return m_levelBlocks; }

    // How far off frequency the tuner has been found to be
    float offsetHz() const { return m_afcHz; }

private:
    void setMixer();
    void iqSample(float zi, float zq);
    void endBlock();
    void measureLevels();

    int m_channel = 0;

    // The mixer, a rotating phasor renormalised now and then
    float m_rotI = 1, m_rotQ = 0, m_stepI = 1, m_stepQ = 0;
    int m_rotAge = 0;
    float m_afcHz = 0;

    fm::Fir m_firstI, m_firstQ, m_chanI, m_chanQ, m_voice;
    int m_dec = 0;
    float m_prevI = 0, m_prevQ = 0;
    float m_de = 0, m_dcIn = 0, m_dcOut = 0;
    float m_gain = 0;

    double m_sumMag = 0, m_sumPow = 0, m_sumDisc = 0, m_sumVoice2 = 0;
    int m_blockN = 0;
    float m_snrDb = 0;
    float m_modulation = 0;
    bool m_carrier = false;
    bool m_open = false;
    int m_sinceSpeech = 0;
    int m_settle = 0;

    // Strength of the channels
    std::vector<float> m_fftI, m_fftQ, m_power;
    int m_fftFill = 0;
    int m_fftWait = 0;
    int m_fftCount = 0;
    std::array<float, kChannels> m_levelsDb {};
    int m_levelBlocks = 0;
};

} // namespace wx
