#include "ais.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ais {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kDecimate = 6;
constexpr int kSamplesPerBit = 5;    // at 48 kHz
// The channel filter: AIS fills about 7 kHz each side of its frequency
constexpr int kTaps = 61;
constexpr double kCutoffHz = 7500;
// How quickly the slicing level follows a tuner slightly off frequency. Stuffed
// data never stays on one tone for more than about six bits, so this spans
// several times that.
constexpr float kLevelRate = 1.0f / 160;
// The longest message is 1008 bits; this is that with its check and stuffing
constexpr size_t kMaxBits = 1300;
// The same frame from another phase arrives within a bit or two of the first,
// and well within the 26 ms slot a ship could next send in
constexpr uint64_t kDuplicateSamples = 48000 / 100;

uint8_t reversed(uint8_t b)
{
    b = uint8_t((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = uint8_t((b & 0xCC) >> 2 | (b & 0x33) << 2);
    return uint8_t((b & 0xAA) >> 1 | (b & 0x55) << 1);
}

// One reading of the bit stream: undoes NRZI, the bit stuffing and the HDLC
// framing, and hands over what passes the frame check.
class BitDecoder
{
public:
    // True when bit completes a good frame, which is then in frame
    bool push(bool level, std::vector<uint8_t> &frame)
    {
        // A one holds the tone, a zero changes it
        const bool bit = level == m_last;
        m_last = level;

        if (bit) {
            ++m_ones;
            if (m_ones > 6) {
                m_collecting = false;
                m_bits.clear();
                return false;
            }
            if (m_collecting)
                m_bits.push_back(1);
        } else {
            if (m_ones == 5) {
                m_ones = 0;     // stuffed
                return false;
            }
            const bool flag = m_ones == 6;
            m_ones = 0;
            if (flag)
                return endOfFrame(frame);
            if (m_collecting)
                m_bits.push_back(0);
        }
        if (m_bits.size() > kMaxBits) {
            m_collecting = false;
            m_bits.clear();
        }
        return false;
    }

private:
    bool endOfFrame(std::vector<uint8_t> &frame)
    {
        bool good = false;
        // The flag's zero and six ones have gone in already
        if (m_collecting && m_bits.size() >= 7) {
            const size_t bits = m_bits.size() - 7;
            if (bits >= 32 && bits % 8 == 0) {
                std::vector<uint8_t> air(bits / 8, 0);
                for (size_t i = 0; i < bits; ++i)
                    if (m_bits[i])
                        air[i / 8] |= uint8_t(1 << (i % 8));
                if (fcs(air.data(), air.size()) == kFcsGood) {
                    frame.clear();
                    for (size_t i = 0; i + 2 < air.size(); ++i)
                        frame.push_back(reversed(air[i]));
                    good = true;
                }
            }
        }
        // A flag both ends one frame and can begin the next
        m_collecting = true;
        m_bits.clear();
        return good;
    }

    bool m_last = false;
    int m_ones = 0;
    bool m_collecting = false;
    std::vector<uint8_t> m_bits;
};

uint32_t bitsAt(const std::vector<uint8_t> &data, int first, int count)
{
    uint32_t value = 0;
    for (int bit = first; bit < first + count; ++bit)
        value = (value << 1) | ((data[size_t(bit / 8)] >> (7 - bit % 8)) & 1);
    return value;
}

int32_t signedBitsAt(const std::vector<uint8_t> &data, int first, int count)
{
    const uint32_t raw = bitsAt(data, first, count);
    const uint32_t sign = 1u << (count - 1);
    return int32_t(raw ^ sign) - int32_t(sign);
}

// Six-bit text: trailing @ and spaces are padding
std::string textAt(const std::vector<uint8_t> &data, int first, int chars)
{
    std::string text;
    for (int i = 0; i < chars; ++i) {
        const uint32_t v = bitsAt(data, first + i * 6, 6);
        if (v == 0)
            break;
        text += char(v < 32 ? v + 64 : v);
    }
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    return text;
}

void setPosition(Track &track, const std::vector<uint8_t> &data, int lonBit, double now)
{
    const double lon = signedBitsAt(data, lonBit, 28) / 600000.0;
    const double lat = signedBitsAt(data, lonBit + 28, 27) / 600000.0;
    // 181 and 91 are how "no position" is sent
    if (std::abs(lon) > 180 || std::abs(lat) > 90)
        return;
    track.lat = lat;
    track.lon = lon;
    track.hasPosition = true;
    track.positionTime = now;
}

void setMotion(Track &track, const std::vector<uint8_t> &data, int speedBit, int courseBit)
{
    const uint32_t speed = bitsAt(data, speedBit, 10);
    const uint32_t course = bitsAt(data, courseBit, 12);
    // 1023 and 3600 are how "not available" is sent
    track.hasSpeed = speed != 1023;
    if (track.hasSpeed)
        track.speedKn = speed / 10.0;
    track.hasCourse = course < 3600;
    if (track.hasCourse)
        track.course = course / 10.0;
}

} // namespace

uint16_t fcs(const uint8_t *bytes, size_t count, uint16_t start)
{
    uint16_t crc = start;
    for (size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1) ? uint16_t((crc >> 1) ^ 0x8408) : uint16_t(crc >> 1);
    }
    return crc;
}

class Channel
{
public:
    explicit Channel(int offsetHz)
    {
        // 288 samples are a whole number of cycles of a 25 kHz offset
        for (int i = 0; i < kTone; ++i) {
            const double phase = -2 * kPi * offsetHz * i / kSampleRate;
            m_toneI[size_t(i)] = float(std::cos(phase));
            m_toneQ[size_t(i)] = float(std::sin(phase));
        }
        // Windowed sinc
        double sum = 0;
        for (int i = 0; i < kTaps; ++i) {
            const double t = i - (kTaps - 1) / 2.0;
            const double x = 2 * kCutoffHz / kSampleRate;
            const double sinc = t == 0 ? 1 : std::sin(kPi * x * t) / (kPi * x * t);
            const double window = 0.54 - 0.46 * std::cos(2 * kPi * i / (kTaps - 1));
            m_taps[size_t(i)] = float(sinc * window);
            sum += sinc * window;
        }
        for (float &t : m_taps)
            t = float(t / sum);
    }

    void push(float i, float q, std::vector<Frame> &out)
    {
        const float tI = m_toneI[size_t(m_tonePos)], tQ = m_toneQ[size_t(m_tonePos)];
        m_tonePos = (m_tonePos + 1) % kTone;
        // Kept twice over, so the filter reads straight through
        const float mixedI = i * tI - q * tQ, mixedQ = i * tQ + q * tI;
        m_pos = m_pos == 0 ? kTaps - 1 : m_pos - 1;
        m_i[size_t(m_pos)] = m_i[size_t(m_pos + kTaps)] = mixedI;
        m_q[size_t(m_pos)] = m_q[size_t(m_pos + kTaps)] = mixedQ;
        if (++m_decimate < kDecimate)
            return;
        m_decimate = 0;

        float fi = 0, fq = 0;
        const float *bi = &m_i[size_t(m_pos)], *bq = &m_q[size_t(m_pos)];
        for (int k = 0; k < kTaps; ++k) {
            fi += m_taps[size_t(k)] * bi[k];
            fq += m_taps[size_t(k)] * bq[k];
        }
        onSample(fi, fq, out);
    }

private:
    static constexpr int kTone = 288;

    void onSample(float i, float q, std::vector<Frame> &out)
    {
        // The change of phase since the last sample is the tone
        const float freq = std::atan2(q * m_prevI - i * m_prevQ, i * m_prevI + q * m_prevQ);
        m_prevI = i;
        m_prevQ = q;
        ++m_count;

        m_level += (freq - m_level) * kLevelRate;
        // Three samples around the middle of a bit, for less noise
        const float sliced = freq + m_f1 + m_f2 - 3 * m_level;
        m_f2 = m_f1;
        m_f1 = freq;

        const int phase = int(m_count % kSamplesPerBit);
        if (m_decoders[size_t(phase)].push(sliced > 0, m_frame) && (m_frame != m_recent || m_count - m_recentAt > kDuplicateSamples)) {
            m_recent = m_frame;
            m_recentAt = m_count;
            out.push_back(Frame { m_frame });
        }
    }

    std::array<float, kTone> m_toneI {}, m_toneQ {};
    int m_tonePos = 0;
    std::array<float, kTaps> m_taps {};
    std::array<float, kTaps * 2> m_i {}, m_q {};
    int m_pos = 0;
    int m_decimate = 0;
    float m_prevI = 0, m_prevQ = 0;
    float m_level = 0, m_f1 = 0, m_f2 = 0;
    uint64_t m_count = 0;
    std::array<BitDecoder, kSamplesPerBit> m_decoders;
    std::vector<uint8_t> m_frame, m_recent;
    uint64_t m_recentAt = 0;
};

Demodulator::Demodulator()
{
    for (const int offset : kChannelOffsetHz)
        m_channels.push_back(new Channel(offset));
}

Demodulator::~Demodulator()
{
    for (Channel *c : m_channels)
        delete c;
}

void Demodulator::process(const uint8_t *iq, size_t bytes, std::vector<Frame> &out)
{
    for (size_t n = 0; n + 1 < bytes; n += 2) {
        // The dongle's zero is 127.4
        const float i = float(iq[n]) - 127.4f, q = float(iq[n + 1]) - 127.4f;
        for (Channel *c : m_channels)
            c->push(i, q, out);
    }
}

bool Tracker::add(const Frame &frame, double now)
{
    const std::vector<uint8_t> &d = frame.bytes;
    if (d.size() < 20)
        return false;
    const int bits = int(d.size()) * 8;
    const uint32_t type = bitsAt(d, 0, 6);
    const uint32_t mmsi = bitsAt(d, 8, 30);
    if (mmsi == 0)
        return false;

    // Each message's length is fixed. Anything else is not what it says it is.
    const bool used = ((type >= 1 && type <= 3) && bits == 168) || (type == 5 && bits == 424)
        || (type == 18 && bits == 168) || (type == 19 && bits == 312)
        || (type == 24 && (bits == 160 || bits == 168));
    if (!used)
        return false;

    Track &track = m_tracks[mmsi];
    track.mmsi = mmsi;
    track.lastHeard = now;
    ++track.messages;

    if (type <= 3) {
        setMotion(track, d, 50, 116);
        setPosition(track, d, 61, now);
    } else if (type == 5) {
        const std::string name = textAt(d, 112, 20);
        if (!name.empty())
            track.name = name;
        track.shipType = int(bitsAt(d, 232, 8));
        track.destination = textAt(d, 302, 20);
    } else if (type == 18 || type == 19) {
        setMotion(track, d, 46, 112);
        setPosition(track, d, 57, now);
        if (type == 19) {
            const std::string name = textAt(d, 143, 20);
            if (!name.empty())
                track.name = name;
            track.shipType = int(bitsAt(d, 263, 8));
        }
    } else if (type == 24) {
        const uint32_t part = bitsAt(d, 38, 2);
        if (part == 0) {
            const std::string name = textAt(d, 40, 20);
            if (!name.empty())
                track.name = name;
        } else if (part == 1) {
            track.shipType = int(bitsAt(d, 40, 8));
        }
    }
    return true;
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

} // namespace ais
