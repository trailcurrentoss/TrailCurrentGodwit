// Checks the decoder against AIS messages with published answers, then transmits
// those messages the way a ship would (framed, stuffed, GMSK, on either channel,
// at any moment, a little off frequency, in noise) and checks they come back
// out. Also checks that noise alone produces nothing.

#include "ais.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
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

// Published AIVDM sentences' payloads, from the GPSD project's AIS decoding notes
// and libais's tests. Sentence armour: six bits a character.
constexpr const char *kPosition = "177KQJ5000G?tO`K>RA1wUbN0TKH";   // 477553000, 47.5828, -122.3458, course 51
constexpr const char *kStatic = "55?MbV02;H;s<HtKR20EHE:0@T4@Dn2222222216L961O5Gf0NSQEp6ClRp888888888880";   // 351759000, EVER DIADEM, NEW YORK, cargo
constexpr int kStaticFill = 2;
constexpr const char *kClassB = "B5NJ;PP005l4ot5Isbl03wsUkP06";     // 367430530
constexpr const char *kPartA = "H42O55i18tMET00000000000000";       // 271041815, PROGUY
constexpr int kPartAFill = 2;

ais::Frame frameOf(const char *armoured, int fill = 0)
{
    std::vector<int> bits;
    for (const char *c = armoured; *c; ++c) {
        int v = *c - 48;
        if (v > 40)
            v -= 8;
        for (int b = 5; b >= 0; --b)
            bits.push_back((v >> b) & 1);
    }
    bits.resize(bits.size() - size_t(fill));
    ais::Frame f;
    f.bytes.assign(bits.size() / 8, 0);
    for (size_t i = 0; i + 7 < bits.size() + 7 && i / 8 < f.bytes.size(); ++i)
        if (bits[i])
            f.bytes[i / 8] |= uint8_t(0x80 >> (i % 8));
    return f;
}

uint8_t reversed(uint8_t b)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i)
        if (b & (1 << i))
            r |= uint8_t(0x80 >> i);
    return r;
}

// The bits a ship keys for a frame: training, flag, stuffed data and check, flag
std::vector<int> keyed(const ais::Frame &f)
{
    std::vector<uint8_t> air;
    for (const uint8_t b : f.bytes)
        air.push_back(reversed(b));
    const uint16_t check = uint16_t(~ais::fcs(air.data(), air.size()));
    air.push_back(uint8_t(check & 0xFF));
    air.push_back(uint8_t(check >> 8));

    std::vector<int> bits;
    for (int i = 0; i < 24; ++i)
        bits.push_back(i % 2);
    const auto flag = [&] {
        for (const int b : { 0, 1, 1, 1, 1, 1, 1, 0 })
            bits.push_back(b);
    };
    flag();
    int ones = 0;
    for (const uint8_t b : air) {
        for (int i = 0; i < 8; ++i) {
            const int bit = (b >> i) & 1;
            bits.push_back(bit);
            ones = bit ? ones + 1 : 0;
            if (ones == 5) {
                bits.push_back(0);
                ones = 0;
            }
        }
    }
    flag();
    return bits;
}

struct Burst
{
    ais::Frame frame;
    double at;           // seconds
    int channelHz;       // from the tuner
};

// GMSK at 9600 bit/s with BT 0.4, as the standard has it, added onto noise.
std::vector<uint8_t> transmit(const std::vector<Burst> &bursts, double seconds, double amplitude, double noise, double errorHz, uint32_t seed)
{
    constexpr int kPerBit = ais::kSampleRate / 9600;
    const size_t n = size_t(seconds * ais::kSampleRate);
    std::vector<std::complex<double>> air(n);

    // The frequency pulse: a bit's rectangle through the Gaussian filter
    std::vector<double> pulse;
    const double sigma = std::sqrt(std::log(2.0)) / (2 * kPi * 0.4);
    for (int i = -2 * kPerBit; i < 3 * kPerBit; ++i) {
        const double t = (i + 0.5) / kPerBit;
        const auto q = [&](double x) { return 0.5 * std::erfc(x / (sigma * std::sqrt(2.0))); };
        pulse.push_back(q(t - 1) - q(t));
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> turn(0, 2 * kPi);
    for (const Burst &b : bursts) {
        const std::vector<int> bits = keyed(b.frame);
        std::vector<double> freq(bits.size() * kPerBit + pulse.size(), 0.0);
        int tone = 1;
        for (size_t k = 0; k < bits.size(); ++k) {
            // NRZI: a zero changes the tone
            if (!bits[k])
                tone = -tone;
            for (size_t p = 0; p < pulse.size(); ++p)
                freq[k * kPerBit + p] += tone * pulse[p] * 2400;
        }
        double phase = turn(rng);
        const size_t start = size_t(b.at * ais::kSampleRate);
        for (size_t s = 0; s < freq.size() && start + s < n; ++s) {
            phase += 2 * kPi * (freq[s] + b.channelHz + errorHz) / ais::kSampleRate;
            air[start + s] += std::polar(amplitude, phase);
        }
    }

    std::normal_distribution<double> gauss(0, noise);
    std::vector<uint8_t> iq(n * 2);
    for (size_t s = 0; s < n; ++s) {
        iq[s * 2] = uint8_t(std::clamp(127.4 + air[s].real() + gauss(rng), 0.0, 255.0));
        iq[s * 2 + 1] = uint8_t(std::clamp(127.4 + air[s].imag() + gauss(rng), 0.0, 255.0));
    }
    return iq;
}

std::vector<ais::Frame> receive(const std::vector<uint8_t> &iq)
{
    ais::Demodulator demod;
    std::vector<ais::Frame> out;
    for (size_t at = 0; at < iq.size(); at += 16384)
        demod.process(iq.data() + at, std::min<size_t>(16384, iq.size() - at), out);
    return out;
}

// How many of count bursts come back, at every moment within a bit
int recovered(double amplitude, double noise, double errorHz, int channelHz, int count, uint32_t seed)
{
    const ais::Frame f = frameOf(kPosition);
    std::vector<Burst> bursts;
    double t = 0.01;
    for (int i = 0; i < count; ++i) {
        bursts.push_back({ f, t, channelHz });
        t += 0.03 + 1.0 / 9600 * (i % 10) / 10;
    }
    int good = 0;
    for (const ais::Frame &o : receive(transmit(bursts, t + 0.01, amplitude, noise, errorHz, seed)))
        good += o.bytes == f.bytes;
    return good;
}

} // namespace

int main()
{
    // ---- The frame check, by the standard's own check value for "123456789"
    {
        const char *digits = "123456789";
        const uint16_t c = uint16_t(~ais::fcs(reinterpret_cast<const uint8_t *>(digits), 9));
        check(c == 0x906E, "frame check of the standard test string");
    }

    // ---- The published messages
    {
        ais::Tracker tracker;
        check(tracker.add(frameOf(kPosition), 0), "a position report is used");
        const ais::Track &t = tracker.tracks().at(477553000);
        check(t.hasPosition && near(t.lat, 47.58283, 0.0001) && near(t.lon, -122.34583, 0.0001),
              "position: " + std::to_string(t.lat) + ", " + std::to_string(t.lon));
        check(t.hasSpeed && near(t.speedKn, 0, 0.01), "speed: " + std::to_string(t.speedKn));
        check(t.hasCourse && near(t.course, 51, 0.1), "course: " + std::to_string(t.course));
        check(!ais::confirmed(t), "one message does not yet confirm a ship");
    }
    {
        ais::Tracker tracker;
        check(tracker.add(frameOf(kStatic, kStaticFill), 0), "a static report is used");
        const ais::Track &t = tracker.tracks().at(351759000);
        check(t.name == "EVER DIADEM", "name: " + t.name);
        check(t.destination == "NEW YORK", "destination: " + t.destination);
        check(t.shipType == 70, "type: " + std::to_string(t.shipType));
        check(!t.hasPosition, "a static report gives no position");
    }
    {
        ais::Tracker tracker;
        check(tracker.add(frameOf(kClassB), 0), "a class B report is used");
        const ais::Track &t = tracker.tracks().at(367430530);
        check(t.hasPosition && std::abs(t.lat) <= 90 && std::abs(t.lon) <= 180,
              "class B position: " + std::to_string(t.lat) + ", " + std::to_string(t.lon));
    }
    {
        ais::Tracker tracker;
        check(tracker.add(frameOf(kPartA, kPartAFill), 0), "a class B name is used");
        check(tracker.tracks().at(271041815).name == "PROGUY", "class B name: " + tracker.tracks().at(271041815).name);
    }
    {
        ais::Tracker tracker;
        ais::Frame truncated = frameOf(kPosition);
        truncated.bytes.resize(20);
        check(!tracker.add(truncated, 0) && tracker.tracks().empty(), "a message of the wrong length is not used");
        tracker.add(frameOf(kPosition), 0);
        tracker.add(frameOf(kPosition), 5);
        check(ais::confirmed(tracker.tracks().at(477553000)), "two messages confirm a ship");
        check(tracker.expire(500).empty(), "a ship heard within ten minutes stays");
        check(tracker.expire(700).size() == 1, "one not heard for ten minutes goes");
    }

    // ---- Over the air
    {
        check(recovered(50, 2, 0, 25000, 100, 1) == 100, "strong, upper channel");
        check(recovered(50, 2, 0, -25000, 100, 2) == 100, "strong, lower channel");
        const int off = recovered(50, 2, 400, 25000, 100, 3);
        check(off >= 98, "400 Hz off frequency: " + std::to_string(off) + " of 100");
        const int fair = recovered(14, 5, 0, 25000, 100, 4);
        check(fair >= 80, "fair signal: " + std::to_string(fair) + " of 100");
    }
    {
        // The same frame arrives once, however many phases of the decoder read it
        const ais::Frame f = frameOf(kPosition);
        const std::vector<ais::Frame> out = receive(transmit({ { f, 0.01, 25000 } }, 0.1, 50, 2, 0, 5));
        check(out.size() == 1, "one burst is one frame: " + std::to_string(out.size()));
    }
    {
        // The longest message, the one most stuffing can hurt
        const ais::Frame f = frameOf(kStatic, kStaticFill);
        const std::vector<ais::Frame> out = receive(transmit({ { f, 0.01, -25000 } }, 0.15, 50, 2, 0, 6));
        check(out.size() == 1 && out[0].bytes == f.bytes, "a two-slot static report comes through");
    }
    {
        // Both channels at once, as happens with ships in different slots
        const ais::Frame a = frameOf(kPosition), b = frameOf(kClassB);
        const std::vector<ais::Frame> out = receive(transmit({ { a, 0.01, 25000 }, { b, 0.012, -25000 } }, 0.1, 40, 2, 0, 7));
        check(out.size() == 2, "both channels at the same moment: " + std::to_string(out.size()));
    }
    {
        // Noise alone. Nothing it produces may become a ship on the screen.
        ais::Tracker tracker;
        const std::vector<ais::Frame> out = receive(transmit({}, 30, 0, 6, 0, 8));
        for (const ais::Frame &f : out)
            tracker.add(f, 0);
        int shown = 0;
        for (const auto &t : tracker.tracks())
            shown += ais::confirmed(t.second);
        check(shown == 0, "thirty seconds of noise show no ships (" + std::to_string(out.size()) + " stray frames)");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
