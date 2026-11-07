// Checks the decoder against ADS-B messages with published answers, then builds
// those messages into I/Q the way a dongle would see them (arriving at any
// fraction of a sample, in noise) and checks they come back out. Also checks
// that noise alone produces nothing.

#include "adsb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

bool near(double a, double b, double within)
{
    return std::abs(a - b) <= within;
}

adsb::Frame frameOf(const char *hex)
{
    adsb::Frame f;
    for (int i = 0; i < 14; ++i) {
        char pair[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
        f.bytes[i] = uint8_t(strtoul(pair, nullptr, 16));
    }
    return f;
}

// Published worked examples, from "The 1090 Megahertz Riddle" (Junzi Sun)
constexpr const char *kCall = "8D4840D6202CC371C32CE0576098";     // KLM1023, category A0
constexpr const char *kEven = "8D40621D58C382D690C8AC2863A7";     // 38,000 ft
constexpr const char *kOdd = "8D40621D58C386435CC412692AD6";      // together: 52.2572, 3.9194
constexpr const char *kGround = "8D485020994409940838175B284F";   // 159 kt over the ground
constexpr const char *kAir = "8DA05F219B06B6AF189400CBC33F";      // 375 kt through the air

// Writes frames into I/Q at the tuner's rate: each half-microsecond chip is
// averaged over the samples it falls in, as the tuner's filter and the chip's
// decimator do, then noise goes on top. Returns unsigned 8-bit I/Q.
std::vector<uint8_t> synthesise(const std::vector<std::pair<adsb::Frame, double>> &frames, double seconds, double amplitude,
                                double noise, uint32_t seed)
{
    const size_t n = size_t(seconds * adsb::kSampleRate);
    std::vector<double> envelope(n, 0.0);

    const auto pulse = [&](double fromMicro) {
        const double a = fromMicro * adsb::kSampleRate / 1e6, b = a + 0.5 * adsb::kSampleRate / 1e6;
        for (size_t s = size_t(a); s <= size_t(b) && s < n; ++s) {
            const double overlap = std::min(double(s + 1), b) - std::max(double(s), a);
            if (overlap > 0)
                envelope[s] += overlap;
        }
    };
    for (const auto &f : frames) {
        const double t = f.second * 1e6;
        for (const double p : { 0.0, 1.0, 3.5, 4.5 })
            pulse(t + p);
        for (int bit = 0; bit < 112; ++bit) {
            const bool one = (f.first.bytes[bit / 8] >> (7 - bit % 8)) & 1;
            pulse(t + 8 + bit + (one ? 0.0 : 0.5));
        }
    }

    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0, noise);
    std::uniform_real_distribution<double> turn(0, 2 * kPi);
    const double phase = turn(rng);
    std::vector<uint8_t> iq(n * 2);
    for (size_t s = 0; s < n; ++s) {
        const double i = 127.4 + amplitude * envelope[s] * std::cos(phase) + gauss(rng);
        const double q = 127.4 + amplitude * envelope[s] * std::sin(phase) + gauss(rng);
        iq[s * 2] = uint8_t(std::clamp(i, 0.0, 255.0));
        iq[s * 2 + 1] = uint8_t(std::clamp(q, 0.0, 255.0));
    }
    return iq;
}

// How many of count frames come back, spread over the fractions of a sample
int recovered(double amplitude, double noise, int count, uint32_t seed)
{
    std::vector<std::pair<adsb::Frame, double>> frames;
    const adsb::Frame f = frameOf(kEven);
    double t = 0.001;
    for (int i = 0; i < count; ++i) {
        frames.push_back({ f, t });
        t += 400e-6 + 0.0417e-6 * (i % 10);  // ten steps across one sample
    }
    const std::vector<uint8_t> iq = synthesise(frames, t + 0.001, amplitude, noise, seed);
    adsb::Demodulator demod;
    std::vector<adsb::Frame> out;
    // The dongle's own buffer size, so frames straddle the pieces
    for (size_t at = 0; at < iq.size(); at += 65536)
        demod.process(iq.data() + at, std::min<size_t>(65536, iq.size() - at), out);
    int good = 0;
    for (const auto &o : out)
        good += !std::memcmp(o.bytes, f.bytes, 14);
    return good;
}

} // namespace

int main()
{
    // ---- Parity and repair
    check(adsb::parity(frameOf(kCall).bytes) == 0, "parity of an intact frame is zero");
    {
        adsb::Frame f = frameOf(kCall);
        f.bytes[6] ^= 0x10;
        check(adsb::parity(f.bytes) != 0, "parity notices a flipped bit");
    }

    // ---- The published messages
    {
        adsb::Tracker tracker;
        tracker.add(frameOf(kCall), 0);
        const adsb::Track &t = tracker.tracks().at(0x4840D6);
        check(t.call == "KLM1023", "callsign reads KLM1023: " + t.call);
        check(!adsb::confirmed(t), "one message does not yet confirm an aircraft");
    }
    {
        adsb::Tracker tracker;
        tracker.add(frameOf(kEven), 1);
        const adsb::Track &first = tracker.tracks().at(0x40621D);
        check(first.hasAltitude && first.altitudeFt == 38000, "altitude reads 38,000 ft: " + std::to_string(first.altitudeFt));
        check(!first.hasPosition, "one half of a position gives no position");
        tracker.add(frameOf(kOdd), 2);
        const adsb::Track &t = tracker.tracks().at(0x40621D);
        check(t.hasPosition, "the pair gives a position");
        // The worked answer uses the even frame as newest; odd is here
        check(near(t.lat, 52.2658, 0.001) && near(t.lon, 3.9389, 0.001),
              "position with the odd newest: " + std::to_string(t.lat) + ", " + std::to_string(t.lon));
        check(adsb::confirmed(t), "two messages confirm it");
    }
    {
        adsb::Tracker tracker;
        tracker.add(frameOf(kOdd), 1);
        tracker.add(frameOf(kEven), 2);
        const adsb::Track &t = tracker.tracks().at(0x40621D);
        check(t.hasPosition && near(t.lat, 52.2572, 0.001) && near(t.lon, 3.9194, 0.001),
              "position with the even newest: " + std::to_string(t.lat) + ", " + std::to_string(t.lon));
    }
    {
        adsb::Tracker tracker;
        tracker.add(frameOf(kOdd), 0);
        tracker.add(frameOf(kEven), 30);
        check(!tracker.tracks().at(0x40621D).hasPosition, "a stale pair gives no position");
    }
    {
        adsb::Tracker tracker;
        tracker.add(frameOf(kGround), 0);
        const adsb::Track &t = tracker.tracks().at(0x485020);
        check(t.hasSpeed && t.speedKt == 159, "ground speed reads 159 kt: " + std::to_string(t.speedKt));
        adsb::Tracker air;
        air.add(frameOf(kAir), 0);
        check(air.tracks().at(0xA05F21).speedKt == 375, "airspeed reads 375 kt: " + std::to_string(air.tracks().at(0xA05F21).speedKt));
    }
    {
        // Position pairs for one aircraft, half a world apart in a moment
        adsb::Tracker tracker;
        tracker.add(frameOf(kEven), 0);
        tracker.add(frameOf(kOdd), 1);
        const double lat = tracker.tracks().at(0x40621D).lat;
        // Rebuild the same messages with a latitude field far from the first
        adsb::Frame even = frameOf(kEven);
        even.bytes[7] ^= 0x40;
        // Parity is ignored by the tracker; only the position matters here
        tracker.add(even, 1.5);
        check(near(tracker.tracks().at(0x40621D).lat, lat, 0.2), "a position jump no aircraft could make is ignored");
    }
    {
        adsb::Tracker tracker;
        adsb::Frame f = frameOf(kCall);
        f.corrected = true;
        tracker.add(f, 0);
        check(tracker.tracks().empty(), "a repaired frame cannot introduce an aircraft");
        tracker.add(frameOf(kCall), 0);
        f.corrected = true;
        tracker.add(f, 1);
        check(tracker.tracks().at(0x4840D6).messages == 2, "a repaired frame adds to one already heard");
        check(tracker.expire(30).empty(), "an aircraft heard recently stays");
        check(tracker.expire(100).size() == 1 && tracker.tracks().empty(), "an aircraft not heard for a minute goes");
    }

    // ---- Through the air, in the I/Q
    {
        // Signal to noise is the pulse's power over the noise's in the tuner's
        // 2.4 MHz. A pulse split across two samples is the hard case, which is
        // what limits the weak end.
        const int strong = recovered(60, 2, 500, 1);
        check(strong == 500, "29 dB, every phase of a sample: " + std::to_string(strong) + " of 500");
        const int good = recovered(40, 5, 500, 2);
        check(good >= 450, "15 dB: " + std::to_string(good) + " of 500");
        const int fair = recovered(30, 5, 500, 3);
        check(fair >= 300, "12.6 dB: " + std::to_string(fair) + " of 500");
    }
    {
        // A frame with one bit damaged in the air
        adsb::Frame f = frameOf(kEven);
        f.bytes[9] ^= 0x04;
        const std::vector<uint8_t> iq = synthesise({ { f, 0.001 } }, 0.002, 60, 2, 4);
        adsb::Demodulator demod;
        std::vector<adsb::Frame> out;
        demod.process(iq.data(), iq.size(), out);
        check(out.size() == 1 && out[0].corrected && !std::memcmp(out[0].bytes, frameOf(kEven).bytes, 14),
              "one damaged bit is repaired and marked");
    }
    {
        // Noise alone, ten seconds of it. Nothing it produces may become an
        // aircraft on the screen.
        const std::vector<uint8_t> iq = synthesise({}, 10, 0, 6, 5);
        adsb::Demodulator demod;
        adsb::Tracker tracker;
        std::vector<adsb::Frame> out;
        for (size_t at = 0; at < iq.size(); at += 65536)
            demod.process(iq.data() + at, std::min<size_t>(65536, iq.size() - at), out);
        for (const auto &f : out)
            tracker.add(f, 0);
        int shown = 0;
        for (const auto &t : tracker.tracks())
            shown += adsb::confirmed(t.second);
        check(shown == 0, "ten seconds of noise show no aircraft (" + std::to_string(out.size()) + " stray frames)");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
