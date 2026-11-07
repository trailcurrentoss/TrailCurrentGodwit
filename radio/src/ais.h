#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Ships from their AIS broadcasts: HDLC frames in GMSK at 9600 bit/s, on 161.975
// and 162.025 MHz. A ship uses both, in turn, so both are always received: the
// tuner sits between them, clear of either, and each is taken out of the I/Q.
//
//   288 kS/s I/Q -> (mix the channel to zero, filter, / 6) -> 48 kHz
//   48 kHz       -> discriminator                          -> 5 samples a bit
//
// Bits are read at all five phases of that, each its own decoder, and the frame
// check says which were right. That needs no clock recovery, which a burst of a
// few tens of milliseconds gives little time for anyway.
namespace ais {

constexpr int kCenterHz = 162000000;
constexpr int kSampleRate = 288000;
constexpr int kChannels = 2;
constexpr int kChannelOffsetHz[kChannels] = { -25000, 25000 };

struct Frame
{
    // The message in the standard's bit order, each byte's top bit first,
    // without the frame check
    std::vector<uint8_t> bytes;
};

// The 16-bit frame check of bytes as they come off the air, lowest bit first.
// Over a frame and its check, an intact one gives kFcsGood.
constexpr uint16_t kFcsGood = 0xF0B8;
uint16_t fcs(const uint8_t *bytes, size_t count, uint16_t start = 0xFFFF);

class Channel;

class Demodulator
{
public:
    Demodulator();
    ~Demodulator();
    // Interleaved unsigned 8-bit I/Q, as the dongle gives it, in any size of
    // pieces. Whole frames found are appended to out.
    void process(const uint8_t *iq, size_t bytes, std::vector<Frame> &out);

private:
    std::vector<Channel *> m_channels;
};

struct Track
{
    uint32_t mmsi = 0;
    std::string name;
    std::string destination;
    int shipType = 0;   // the standard's code; 0 until sent
    bool hasPosition = false;
    double lat = 0, lon = 0;
    bool hasSpeed = false;
    double speedKn = 0;
    bool hasCourse = false;
    double course = 0;
    double lastHeard = 0;
    double positionTime = -100000;
    int messages = 0;
};

// What is known about each ship heard, from the frames decoded
class Tracker
{
public:
    // Now is seconds on any steady clock. False if the frame said nothing used.
    bool add(const Frame &frame, double now);
    // Forgets ships not heard for this long and returns their numbers. A ship at
    // anchor reports every 3 minutes and its name every 6, so this is patient.
    std::vector<uint32_t> expire(double now, double afterSeconds = 600);
    const std::map<uint32_t, Track> &tracks() const { return m_tracks; }

private:
    std::map<uint32_t, Track> m_tracks;
};

// Two messages make a ship worth showing. Noise can pass a 16-bit check once in
// a long while, but not twice with the same number.
inline bool confirmed(const Track &track) { return track.messages >= 2; }

} // namespace ais
