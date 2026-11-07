#include "adsb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace adsb {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kBits = 112;
constexpr double kSamplesPerMicro = kSampleRate / 1e6;
constexpr double kSamplesPerBit = kSamplesPerMicro;
constexpr double kPreambleSamples = 8 * kSamplesPerMicro;
// Preamble and frame, whole samples, rounded up
constexpr size_t kFrameSamples = size_t(120 * kSamplesPerMicro) + 2;
// A preamble's pulses must stand this far above the quiet between them. Lower
// finds a few more weak frames at the cost of reading more noise.
constexpr float kPulseOverQuiet = 1.6f;
// The fractions of a sample a frame is read at. Each start sample is tried, so
// these only need to cover the space between two of them.
constexpr double kPhases[] = { 0.0, 0.2, 0.4, 0.6, 0.8 };

// The Mode S generator polynomial, as a table over bytes
const std::array<uint32_t, 256> &crcTable()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t {};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i << 16;
            for (int b = 0; b < 8; ++b)
                c = (c & 0x800000) ? ((c << 1) ^ 0xFFF409) & 0xFFFFFF : (c << 1) & 0xFFFFFF;
            t[i] = c;
        }
        return t;
    }();
    return table;
}

// What flipping each single bit does to the parity. Bits 0 to 4 are the
// downlink format, which has already been used to decide this is a squitter,
// so those are never repaired.
int repairableBit(uint32_t syndrome)
{
    static const std::array<uint32_t, kBits> flips = [] {
        std::array<uint32_t, kBits> f {};
        for (int bit = 0; bit < kBits; ++bit) {
            uint8_t frame[14] = {};
            frame[bit / 8] = uint8_t(0x80 >> (bit % 8));
            f[bit] = parity(frame);
        }
        return f;
    }();
    for (int bit = 5; bit < kBits; ++bit)
        if (flips[bit] == syndrome)
            return bit;
    return -1;
}

// An ADS-B squitter: from a transponder (17), or from equipment that is not one
// and sends the same format (18, control field 0)
bool isSquitter(uint8_t first)
{
    const int df = first >> 3;
    return df == 17 || (df == 18 && (first & 7) == 0);
}

uint32_t bitsAt(const uint8_t *data, int first, int count)
{
    uint32_t value = 0;
    for (int bit = first; bit < first + count; ++bit)
        value = (value << 1) | ((data[bit / 8] >> (7 - bit % 8)) & 1);
    return value;
}

// The number of longitude zones at a latitude
int longitudeZones(double lat)
{
    const double c = std::cos(kPi / 180 * std::abs(lat));
    const double a = 1 - (1 - std::cos(kPi / 30)) / (c * c);
    if (a <= -1)
        return 1;
    return int(std::floor(2 * kPi / std::acos(a)));
}

// A remainder that is never negative
double floorMod(double x, double y)
{
    return x - y * std::floor(x / y);
}

std::string readCall(const uint8_t *me)
{
    static const char kChars[] = "?ABCDEFGHIJKLMNOPQRSTUVWXYZ????? ???????????????0123456789??????";
    std::string call;
    for (int i = 0; i < 8; ++i) {
        const char c = kChars[bitsAt(me, 8 + i * 6, 6)];
        if (c != ' ' && c != '?')
            call += c;
    }
    return call;
}

} // namespace

uint32_t parity(const uint8_t *frame)
{
    const auto &table = crcTable();
    uint32_t crc = 0;
    for (int i = 0; i < 11; ++i)
        crc = ((crc << 8) & 0xFFFFFF) ^ table[((crc >> 16) ^ frame[i]) & 0xFF];
    const uint32_t sent = (uint32_t(frame[11]) << 16) | (uint32_t(frame[12]) << 8) | frame[13];
    return crc ^ sent;
}

Demodulator::Demodulator()
    : m_magnitude(65536)
{
    // The dongle's zero is 127.4
    for (int i = 0; i < 256; ++i)
        for (int q = 0; q < 256; ++q)
            m_magnitude[size_t(i) << 8 | size_t(q)] = float(std::hypot(i - 127.4, q - 127.4));
}

// The signal between two times in samples: a running total, read between its
// entries, so a window may start and end anywhere
double Demodulator::energy(double from, double to) const
{
    const auto at = [this](double t) {
        const size_t whole = size_t(t);
        return m_sum[whole] + (t - double(whole)) * double(m_samples[whole]);
    };
    return at(to) - at(from);
}

bool Demodulator::readAt(size_t start, Frame &frame) const
{
    for (const double phase : kPhases) {
        const double data = double(start) + phase + kPreambleSamples;
        std::memset(frame.bytes, 0, sizeof frame.bytes);
        frame.corrected = false;
        for (int bit = 0; bit < kBits; ++bit) {
            const double t = data + bit * kSamplesPerBit;
            if (energy(t, t + kSamplesPerBit / 2) > energy(t + kSamplesPerBit / 2, t + kSamplesPerBit))
                frame.bytes[bit / 8] |= uint8_t(0x80 >> (bit % 8));
            // Most candidates are noise: the first byte is enough to tell
            if (bit == 7 && !isSquitter(frame.bytes[0]))
                break;
        }
        if (!isSquitter(frame.bytes[0]))
            continue;
        const uint32_t syndrome = parity(frame.bytes);
        if (syndrome == 0)
            return true;
        const int bad = repairableBit(syndrome);
        if (bad >= 0) {
            frame.bytes[bad / 8] ^= uint8_t(0x80 >> (bad % 8));
            frame.corrected = true;
            return true;
        }
    }
    return false;
}

void Demodulator::process(const uint8_t *iq, size_t bytes, std::vector<Frame> &out)
{
    const size_t had = m_samples.size();
    m_samples.resize(had + bytes / 2);
    for (size_t i = 0; i < bytes / 2; ++i)
        m_samples[had + i] = m_magnitude[size_t(iq[i * 2]) << 8 | size_t(iq[i * 2 + 1])];
    if (m_samples.size() < kFrameSamples)
        return;

    m_sum.resize(m_samples.size() + 1);
    m_sum[0] = 0;
    for (size_t i = 0; i < m_samples.size(); ++i)
        m_sum[i + 1] = m_sum[i] + double(m_samples[i]);

    const float *m = m_samples.data();
    const size_t end = m_samples.size() - kFrameSamples;
    size_t i = 0;
    while (i < end) {
        // Pulses at 0, 1, 3.5 and 4.5 microseconds, 0.5 long: samples 0, 2.4,
        // 8.4 and 10.8. Quiet between them, and then until the data at 19.2.
        const float low = std::min({ m[i], std::max(m[i + 2], m[i + 3]), std::max(m[i + 8], m[i + 9]),
                                     std::max(m[i + 10], m[i + 11]) });
        const float quiet = (m[i + 5] + m[i + 6] + m[i + 13] + m[i + 14] + m[i + 15] + m[i + 16]) / 6;
        Frame frame;
        if (low > kPulseOverQuiet * quiet && readAt(i, frame)) {
            out.push_back(frame);
            i += kFrameSamples - 2;
        } else {
            ++i;
        }
    }
    // What is left may be the start of a frame that ends in the next piece
    m_samples.erase(m_samples.begin(), m_samples.begin() + std::ptrdiff_t(i));
}

bool decodePosition(int latEven, int lonEven, int latOdd, int lonOdd, bool oddIsNewer, double &lat, double &lon)
{
    constexpr double kScale = 131072;
    const double y0 = latEven / kScale, y1 = latOdd / kScale, x0 = lonEven / kScale, x1 = lonOdd / kScale;

    const double j = std::floor(59 * y0 - 60 * y1 + 0.5);
    double lat0 = 360.0 / 60 * (floorMod(j, 60) + y0);
    double lat1 = 360.0 / 59 * (floorMod(j, 59) + y1);
    if (lat0 >= 270)
        lat0 -= 360;
    if (lat1 >= 270)
        lat1 -= 360;
    if (std::abs(lat0) > 90 || std::abs(lat1) > 90)
        return false;

    const int zones = longitudeZones(lat0);
    if (zones != longitudeZones(lat1))
        return false;

    const double m = std::floor(x0 * (zones - 1) - x1 * zones + 0.5);
    const int n = std::max(oddIsNewer ? zones - 1 : zones, 1);
    lat = oddIsNewer ? lat1 : lat0;
    lon = 360.0 / n * (floorMod(m, n) + (oddIsNewer ? x1 : x0));
    if (lon >= 180)
        lon -= 360;
    return true;
}

void Tracker::addPosition(Track &track, const uint8_t *me, double now)
{
    const int odd = int(bitsAt(me, 21, 1));
    track.cprLat[odd] = int(bitsAt(me, 22, 17));
    track.cprLon[odd] = int(bitsAt(me, 39, 17));
    track.cprTime[odd] = now;
    // An aircraft covers about 3 nm in 10 seconds even at speed, well inside a
    // zone. Any longer and the pair may not agree.
    if (std::abs(track.cprTime[0] - track.cprTime[1]) > 10)
        return;

    double lat, lon;
    if (!decodePosition(track.cprLat[0], track.cprLon[0], track.cprLat[1], track.cprLon[1], odd == 1, lat, lon))
        return;
    // Faster than anything flying (about 1000 kt, in degrees of latitude a
    // second) means a bad pair, unless it has been a while
    if (track.hasPosition && now - track.positionTime < 60) {
        const double km = std::hypot(lat - track.lat, (lon - track.lon) * std::cos(lat * kPi / 180)) * 111.2;
        if (km > 0.6 * (now - track.positionTime) + 5)
            return;
    }
    track.lat = lat;
    track.lon = lon;
    track.hasPosition = true;
    track.positionTime = now;
}

bool Tracker::add(const Frame &frame, double now)
{
    const uint8_t *b = frame.bytes;
    const uint32_t icao = (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
    auto it = m_tracks.find(icao);
    // A repaired frame only adds to an aircraft already heard clean: it is the
    // one kind of frame that noise can pass as good
    if (it == m_tracks.end()) {
        if (frame.corrected)
            return false;
        it = m_tracks.emplace(icao, Track {}).first;
        it->second.icao = icao;
    }
    Track &track = it->second;
    track.lastHeard = now;
    ++track.messages;

    const uint8_t *me = b + 4;
    const int type = int(me[0] >> 3);
    if (type >= 1 && type <= 4) {
        const std::string call = readCall(me);
        if (!call.empty())
            track.call = call;
        const int subtype = me[0] & 7;
        if (subtype != 0)
            track.category = std::string(1, char('A' + 4 - type)) + char('0' + subtype);
        return true;
    }
    if ((type >= 9 && type <= 18) || (type >= 20 && type <= 22)) {
        const uint32_t raw = bitsAt(me, 8, 12);
        if (type >= 20) {
            // Height from GNSS, in metres. Only until a barometric one is heard.
            if (raw != 0) {
                track.altitudeFt = int(std::lround(raw * 3.28084));
                track.hasAltitude = true;
            }
        } else if (raw & 0x10) {
            // The other coding is from decades-old transponders, which do not
            // send ADS-B
            const uint32_t n = ((raw & 0xFE0) >> 1) | (raw & 0xF);
            track.altitudeFt = int(n) * 25 - 1000;
            track.hasAltitude = true;
        }
        addPosition(track, me, now);
        return true;
    }
    if (type == 19) {
        const int subtype = me[0] & 7;
        if (subtype == 1 || subtype == 2) {
            const int ew = int(bitsAt(me, 14, 10));
            const int ns = int(bitsAt(me, 25, 10));
            if (ew > 0 && ns > 0) {
                const int scale = subtype == 2 ? 4 : 1;
                track.speedKt = int(std::lround(std::hypot(ew - 1, ns - 1) * scale));
                track.hasSpeed = true;
            }
        } else if (subtype == 3 || subtype == 4) {
            // Through the air, where the aircraft cannot see the ground. Less
            // often sent, and only used when there is nothing better.
            const int air = int(bitsAt(me, 25, 10));
            if (air > 0 && !track.hasSpeed) {
                track.speedKt = (air - 1) * (subtype == 4 ? 4 : 1);
                track.hasSpeed = true;
            }
        }
        return true;
    }
    return false;
}

std::vector<uint32_t> Tracker::expire(double now, double afterSeconds)
{
    std::vector<uint32_t> gone;
    for (auto it = m_tracks.begin(); it != m_tracks.end();) {
        if (now - it->second.lastHeard > afterSeconds) {
            gone.push_back(it->first);
            it = m_tracks.erase(it);
        } else {
            ++it;
        }
    }
    return gone;
}

} // namespace adsb
