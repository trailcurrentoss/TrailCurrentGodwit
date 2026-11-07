// Transmits NOAA Weather Radio as the stations do (narrow FM, 5 kHz deviation, on the
// seven channels, a little off frequency, in noise) and checks what the receiver
// makes of it: the right speech on the right channel, nothing from noise or an empty
// carrier, and a strength for all seven at once. Also checks the station table.

#include "wxradio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
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

struct Station
{
    double hz;            // off the tuner
    double level;         // amplitude; full scale is 1
    double toneHz;        // 0 is a carrier with nothing on it
    double phase = 0, tonePhase = 0;
};

double channelOffset(int channel)
{
    return double(wx::kChannelHz[size_t(channel)] - wx::kTunerHz);
}

// Seconds of the air, as the dongle's bytes
std::vector<uint8_t> air(std::vector<Station> &stations, double noise, double seconds, std::mt19937 &rng)
{
    std::normal_distribution<double> gauss(0, noise > 0 ? noise : 1);
    const size_t n = size_t(seconds * wx::kIqRate);
    std::vector<uint8_t> out(n * 2);
    for (size_t i = 0; i < n; ++i) {
        double re = noise > 0 ? gauss(rng) : 0;
        double im = noise > 0 ? gauss(rng) : 0;
        for (Station &s : stations) {
            const double dev = s.toneHz > 0 ? 4000 * std::sin(s.tonePhase) : 0;
            s.tonePhase += 2 * kPi * s.toneHz / wx::kIqRate;
            s.phase += 2 * kPi * (s.hz + dev) / wx::kIqRate;
            re += s.level * std::cos(s.phase);
            im += s.level * std::sin(s.phase);
        }
        out[i * 2] = uint8_t(std::clamp(std::lround(re * 127.5 + 127.5), 0L, 255L));
        out[i * 2 + 1] = uint8_t(std::clamp(std::lround(im * 127.5 + 127.5), 0L, 255L));
    }
    return out;
}

// In the last of the audio: how loud, and how much of it is this tone
struct Heard
{
    double rms = 0;
    double toneShare = 0;
};

Heard listen(const std::vector<int16_t> &pcm, double toneHz, double lastSeconds = 1)
{
    Heard h;
    const size_t n = std::min(pcm.size(), size_t(lastSeconds * wx::kAudioRate));
    if (n == 0)
        return h;
    double power = 0, re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) {
        const double x = pcm[pcm.size() - n + i] / 32768.0;
        power += x * x;
        re += x * std::cos(2 * kPi * toneHz * double(i) / wx::kAudioRate);
        im += x * std::sin(2 * kPi * toneHz * double(i) / wx::kAudioRate);
    }
    power /= double(n);
    const double tone = 2 * (re * re + im * im) / (double(n) * double(n));
    h.rms = std::sqrt(power);
    h.toneShare = power > 0 ? tone / power : 0;
    return h;
}

int strongest(const std::array<float, wx::kChannels> &levels)
{
    return int(std::max_element(levels.begin(), levels.end()) - levels.begin());
}

void testEveryChannel(std::mt19937 &rng)
{
    for (int c = 0; c < wx::kChannels; ++c) {
        wx::Receiver rx;
        rx.setChannel(c);
        std::vector<Station> on { { channelOffset(c), 0.3, 1000 } };
        const std::vector<uint8_t> iq = air(on, 0.01, 2.5, rng);
        std::vector<int16_t> pcm;
        rx.process(iq.data(), iq.size(), pcm);
        const Heard h = listen(pcm, 1000);
        char label[120];
        std::snprintf(label, sizeof label, "channel %d: tone share %.3f, rms %.3f, snr %.1f, level %.1f dB", c, h.toneShare,
                      h.rms, double(rx.snrDb()), double(rx.levelsDb()[size_t(c)]));
        check(rx.receiving() && h.toneShare > 0.9 && h.rms > 0.05, label);
        check(strongest(rx.levelsDb()) == c, "  and it reads as the strongest of the seven");
    }
    wx::Receiver rx;
    rx.setChannel(6);
    std::vector<Station> on { { channelOffset(6), 0.3, 1000 } };
    const std::vector<uint8_t> iq = air(on, 0.01, 2.5, rng);
    std::vector<int16_t> pcm;
    rx.process(iq.data(), iq.size(), pcm);
    float others = 0;
    for (int c = 0; c < 5; ++c)
        others = std::max(others, rx.levelsDb()[size_t(c)]);
    check(others < 6, "channels with nothing on them read under 6 dB, the highest " + std::to_string(others));
}

void testRatesAreExact()
{
    check(wx::kIqRate % wx::kAudioRate == 0, "the audio rate divides the tuner's");
    bool clear = true;
    for (int c = 0; c < wx::kChannels; ++c)
        clear = clear && std::abs(channelOffset(c)) >= 12500;
    check(clear, "no channel sits on the tuner's DC spike");
    check(channelOffset(6) + 8000 < wx::kIqRate / 2 && channelOffset(0) - 8000 > -wx::kIqRate / 2,
          "all seven fit in what the tuner delivers");
}

void testChannelsOnly()
{
    check(wx::channelIndex(162550000) == 6, "162.550 is the last channel");
    check(wx::channelIndex(162400000) == 0, "162.400 is the first");
    check(wx::channelIndex(162412500) == -1, "between two channels is not a channel");
    check(wx::channelIndex(161975000) == -1, "AIS is not a channel");
    check(wx::channelIndex(155000000) == -1, "nothing else is either");
}

void testNoiseIsSilent(std::mt19937 &rng)
{
    wx::Receiver rx;
    rx.setChannel(3);
    std::vector<Station> none;
    const std::vector<uint8_t> iq = air(none, 0.1, 30, rng);
    std::vector<int16_t> pcm;
    rx.process(iq.data(), iq.size(), pcm);
    const Heard h = listen(pcm, 1000);
    char label[120];
    std::snprintf(label, sizeof label, "noise alone stays silent: snr %.1f, rms %.4f", double(rx.snrDb()), h.rms);
    check(!rx.receiving() && h.rms < 0.001, label);
    float top = 0;
    for (float l : rx.levelsDb())
        top = std::max(top, l);
    check(top < 3, "and no channel stands out, the highest " + std::to_string(top));
}

void testBareCarrierIsNotAStation(std::mt19937 &rng)
{
    wx::Receiver rx;
    rx.setChannel(2);
    std::vector<Station> spur { { channelOffset(2), 0.2, 0 } };
    const std::vector<uint8_t> iq = air(spur, 0.005, 3, rng);
    std::vector<int16_t> pcm;
    rx.process(iq.data(), iq.size(), pcm);
    check(!rx.receiving(), "a steady carrier with nothing on it is not a station");
}

void testPausesKeepTheStation(std::mt19937 &rng)
{
    wx::Receiver rx;
    rx.setChannel(4);
    std::vector<Station> talking { { channelOffset(4), 0.3, 1000 } };
    std::vector<uint8_t> iq = air(talking, 0.01, 1.5, rng);
    std::vector<int16_t> pcm;
    rx.process(iq.data(), iq.size(), pcm);
    check(rx.receiving(), "speech opens the channel");
    std::vector<Station> paused { { channelOffset(4), 0.3, 0 } };
    iq = air(paused, 0.01, 5, rng);
    rx.process(iq.data(), iq.size(), pcm);
    check(rx.receiving(), "a five second pause between messages keeps it open");
}

// A station the bars show must be heard, not called empty
void testBarMeansHeard(std::mt19937 &rng)
{
    for (const double level : { 0.04, 0.03, 0.025 }) {
        wx::Receiver rx;
        rx.setChannel(2);
        std::vector<Station> on { { channelOffset(2), level, 1000 } };
        const std::vector<uint8_t> iq = air(on, 0.05, 3, rng);
        std::vector<int16_t> pcm;
        rx.process(iq.data(), iq.size(), pcm);
        const Heard h = listen(pcm, 1000);
        char label[120];
        std::snprintf(label, sizeof label, "a faint station, level %.1f dB, snr %.1f: heard, rms %.3f, tone share %.3f",
                      double(rx.levelsDb()[2]), double(rx.snrDb()), h.rms, h.toneShare);
        check(rx.levelsDb()[2] > 5.5 && rx.receiving() && h.rms > 0.01, label);
    }
}

void testOffFrequency(std::mt19937 &rng)
{
    for (const double off : { 3000.0, -3000.0, 5000.0, -5000.0 }) {
        wx::Receiver rx;
        rx.setChannel(5);
        std::vector<Station> on { { channelOffset(5) + off, 0.3, 1000 } };
        const std::vector<uint8_t> iq = air(on, 0.01, 4, rng);
        std::vector<int16_t> pcm;
        rx.process(iq.data(), iq.size(), pcm);
        const Heard h = listen(pcm, 1000);
        char label[120];
        std::snprintf(label, sizeof label, "a tuner %+.0f Hz out: followed to %+.0f Hz, tone share %.3f", off,
                      double(rx.offsetHz()), h.toneShare);
        check(rx.receiving() && std::abs(double(rx.offsetHz()) - off) < 300 && h.toneShare > 0.9, label);
    }
}

void testNextChannelStaysOut(std::mt19937 &rng)
{
    wx::Receiver rx;
    rx.setChannel(3);
    std::vector<Station> both { { channelOffset(3), 0.08, 1000 }, { channelOffset(4), 0.4, 700 } };
    const std::vector<uint8_t> iq = air(both, 0.005, 2.5, rng);
    std::vector<int16_t> pcm;
    rx.process(iq.data(), iq.size(), pcm);
    const Heard wanted = listen(pcm, 1000), other = listen(pcm, 700);
    char label[120];
    std::snprintf(label, sizeof label, "a station 14 dB stronger on the next channel: wanted %.3f, it %.4f",
                  wanted.toneShare, other.toneShare);
    check(rx.receiving() && wanted.toneShare > 0.8 && other.toneShare < 0.02, label);
    check(strongest(rx.levelsDb()) == 4, "the stronger one reads as the strongest");
}

void testChangingChannel(std::mt19937 &rng)
{
    wx::Receiver rx;
    rx.setChannel(1);
    std::vector<Station> two { { channelOffset(1), 0.2, 1000 }, { channelOffset(6), 0.2, 700 } };
    std::vector<uint8_t> iq = air(two, 0.01, 1.5, rng);
    std::vector<int16_t> pcm;
    rx.process(iq.data(), iq.size(), pcm);
    check(listen(pcm, 1000).toneShare > 0.8, "on the first channel, its speech");
    rx.setChannel(6);
    check(!rx.receiving(), "changing channel closes the squelch until the new one is measured");
    iq = air(two, 0.01, 1.5, rng);
    rx.process(iq.data(), iq.size(), pcm);
    check(rx.receiving() && listen(pcm, 700).toneShare > 0.8, "then the other channel's speech, no retune needed");
}

void testWeakStation(std::mt19937 &rng)
{
    wx::Receiver rx;
    rx.setChannel(0);
    std::vector<Station> on { { channelOffset(0), 0.05, 1000 } };
    const std::vector<uint8_t> iq = air(on, 0.03, 3, rng);
    std::vector<int16_t> pcm;
    rx.process(iq.data(), iq.size(), pcm);
    const Heard h = listen(pcm, 1000);
    char label[120];
    std::snprintf(label, sizeof label, "a weak station: snr %.1f, level %.1f dB, tone share %.3f", double(rx.snrDb()),
                  double(rx.levelsDb()[0]), h.toneShare);
    check(rx.receiving() && h.toneShare > 0.5 && rx.levelsDb()[0] > 6, label);
}

void testTable(const std::string &path)
{
    std::ifstream in(path);
    check(bool(in), "the station table opens: " + path);
    std::string line;
    std::map<int, int> perChannel;
    std::set<std::string> calls;
    int rows = 0, bad = 0;
    bool denver = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t'))
            f.push_back(cell);
        ++rows;
        if (f.size() != 6) {
            ++bad;
            continue;
        }
        const int hz = std::atoi(f[0].c_str());
        const double lat = std::atof(f[2].c_str()), lon = std::atof(f[3].c_str());
        const bool lonOk = (lon >= -180 && lon <= -60) || (lon >= 130 && lon <= 180);
        if (wx::channelIndex(hz) < 0 || lat < -20 || lat > 75 || !lonOk || f[1].empty() || f[4].empty())
            ++bad;
        ++perChannel[hz];
        calls.insert(f[1]);
        if (f[1] == "KEC76" && hz == 162550000 && f[4] == "Denver" && std::abs(lat - 39.7) < 0.5)
            denver = true;
    }
    check(rows > 900, "it has the network's stations: " + std::to_string(rows));
    check(bad == 0, "every row is whole, on a channel and in US territory: " + std::to_string(bad) + " are not");
    check(perChannel.size() == size_t(wx::kChannels), "all seven channels are used");
    check(int(calls.size()) == rows, "each call sign appears once");
    check(denver, "a station looked up by hand is right: Denver, KEC76, 162.550");
}

} // namespace

int main()
{
    std::mt19937 rng(11);
    testRatesAreExact();
    testChannelsOnly();
    testEveryChannel(rng);
    testNoiseIsSilent(rng);
    testBareCarrierIsNotAStation(rng);
    testPausesKeepTheStation(rng);
    testBarMeansHeard(rng);
    testOffFrequency(rng);
    testNextChannelStaysOut(rng);
    testChangingChannel(rng);
    testWeakStation(rng);
    testTable(WX_STATION_TABLE);
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
