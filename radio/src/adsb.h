#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Aircraft from their ADS-B broadcasts: 112-bit Mode S extended squitters on
// 1090 MHz, from raw I/Q.
//
// A squitter is pulse-position modulated at 1 Mbit/s: an 8 microsecond preamble,
// then each bit a half-microsecond pulse in the first half of its microsecond
// for a one, the second half for a zero. At 2.4 MS/s a bit is 2.4 samples, so
// they rarely land on whole samples. Each likely preamble is read at several
// fractions of a sample and the frame's 24-bit parity picks the one that is
// right, which is much more sensitive than slicing at one fixed phase.
namespace adsb {

constexpr int kFrequencyHz = 1090000000;
constexpr int kSampleRate = 2400000;

struct Frame
{
    uint8_t bytes[14];
    // One bit was repaired using the parity
    bool corrected = false;
};

// The 24-bit Mode S parity of a whole 112-bit frame: 0 when it is intact
uint32_t parity(const uint8_t *frame);

class Demodulator
{
public:
    Demodulator();
    // Interleaved unsigned 8-bit I/Q, as the dongle gives it, in any size of
    // pieces. Whole frames found are appended to out.
    void process(const uint8_t *iq, size_t bytes, std::vector<Frame> &out);

private:
    bool readAt(size_t start, Frame &frame) const;
    double energy(double from, double to) const;

    std::vector<float> m_magnitude;   // by I and Q together, 64 K entries
    std::vector<float> m_samples;     // ends with the start of a possible frame
    std::vector<double> m_sum;        // running total of m_samples
};

struct Track
{
    uint32_t icao = 0;
    std::string call;
    // The emitter category as the wire has it: "A3" is a large aircraft
    std::string category;
    bool hasPosition = false;
    double lat = 0, lon = 0;
    bool hasAltitude = false;
    int altitudeFt = 0;
    bool hasSpeed = false;
    int speedKt = 0;
    double lastHeard = 0;
    int messages = 0;

    // The newest compact position of each format, the even [0] and the odd [1]
    int cprLat[2] = { 0, 0 };
    int cprLon[2] = { 0, 0 };
    double cprTime[2] = { -1000, -1000 };
    double positionTime = -1000;
};

// What is known about each aircraft heard, from the frames decoded
class Tracker
{
public:
    // Now is seconds on any steady clock. False if the frame said nothing used.
    bool add(const Frame &frame, double now);
    // Forgets aircraft not heard for this long and returns their addresses
    std::vector<uint32_t> expire(double now, double afterSeconds = 60);
    const std::map<uint32_t, Track> &tracks() const { return m_tracks; }

private:
    void addPosition(Track &track, const uint8_t *me, double now);

    std::map<uint32_t, Track> m_tracks;
};

// Two messages make an aircraft worth showing: noise can pass the parity check
// once in a long while, but not twice with the same address
inline bool confirmed(const Track &track) { return track.messages >= 2; }

// Latitude and longitude from a pair of compact positions, even first. False
// when they straddle a longitude zone and so do not belong together.
bool decodePosition(int latEven, int lonEven, int latOdd, int lonOdd, bool oddIsNewer, double &lat, double &lon);

} // namespace adsb
