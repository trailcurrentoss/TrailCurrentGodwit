#include "dbc.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {
// BO_ 27 TorrentStatus0: 8 Torrent
const QRegularExpression MessageLine(QStringLiteral(R"(^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+\S+)"));
// SG_ Output1Value : 7|8@0+ (1,0) [0|255] "" Tapper,Headwaters
// (a multiplexed signal has an M or m<n> before the colon; those are skipped)
const QRegularExpression SignalLine(QStringLiteral(
    R"(^SG_\s+(\w+)\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*\(\s*([-+0-9.eE]+)\s*,\s*([-+0-9.eE]+)\s*\)\s*\[\s*([-+0-9.eE]+)\s*\|\s*([-+0-9.eE]+)\s*\])"));

// Bit numbering as the DBC has it: bit n is bit n % 8 of byte n / 8, where
// bit 0 is a byte's least significant bit
inline int bitAt(const uint8_t *data, int pos) { return (data[pos / 8] >> (pos % 8)) & 1; }
inline void setBit(uint8_t *data, int pos, int v)
{
    if (v) data[pos / 8] |= uint8_t(1u << (pos % 8));
    else data[pos / 8] &= uint8_t(~(1u << (pos % 8)));
}

// The positions of a signal's bits, most significant first
QList<int> bitPositions(const Dbc::Signal &s)
{
    QList<int> out;
    out.reserve(s.length);
    if (s.bigEndian) {
        // From the MSB, down through each byte, then on to the next byte's bit 7
        int pos = s.start;
        for (int i = 0; i < s.length; ++i) {
            out.append(pos);
            pos = (pos % 8 == 0) ? pos + 15 : pos - 1;
        }
    } else {
        for (int i = s.length - 1; i >= 0; --i)
            out.append(s.start + i);
    }
    return out;
}
}

const Dbc::Signal *Dbc::Message::signal(const QString &name) const
{
    for (const Signal &s : signals_)
        if (s.name == name)
            return &s;
    return nullptr;
}

bool Dbc::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = path + QStringLiteral(": ") + f.errorString();
        m_messages.clear();
        m_byName.clear();
        m_byId.clear();
        return false;
    }
    return parse(f.readAll(), error);
}

bool Dbc::parse(const QByteArray &text, QString *error)
{
    m_messages.clear();
    m_byName.clear();
    m_byId.clear();
    Message *cur = nullptr;
    const QList<QByteArray> lines = text.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1String("BO_ "))) {
            cur = nullptr;
            const auto m = MessageLine.match(line);
            if (!m.hasMatch())
                continue;
            Message msg;
            const quint64 id = m.captured(1).toULongLong();
            // Bit 31 marks a 29-bit identifier
            msg.extended = id & 0x80000000u;
            msg.id = quint32(id & 0x1FFFFFFFu);
            msg.name = m.captured(2);
            msg.dlc = m.captured(3).toInt();
            if (msg.dlc < 0 || msg.dlc > 64 || m_byName.contains(msg.name))
                continue;
            m_messages.append(msg);
            cur = &m_messages.last();
        } else if (line.startsWith(QLatin1String("SG_ ")) && cur) {
            const auto m = SignalLine.match(line);
            if (!m.hasMatch())
                continue;
            Signal s;
            s.name = m.captured(1);
            s.start = m.captured(2).toInt();
            s.length = m.captured(3).toInt();
            s.bigEndian = m.captured(4) == QLatin1String("0");
            s.isSigned = m.captured(5) == QLatin1String("-");
            s.scale = m.captured(6).toDouble();
            s.offset = m.captured(7).toDouble();
            s.min = m.captured(8).toDouble();
            s.max = m.captured(9).toDouble();
            if (s.length < 1 || s.length > 64 || s.scale == 0)
                continue;
            // Every bit inside the message
            const QList<int> bits = bitPositions(s);
            const bool inside = std::all_of(bits.cbegin(), bits.cend(), [&](int p) { return p >= 0 && p < cur->dlc * 8; });
            if (inside)
                cur->signals_.append(s);
        } else if (!line.isEmpty() && !line.startsWith(QLatin1String("SG_ "))) {
            cur = nullptr;
        }
    }
    for (int i = 0; i < m_messages.size(); ++i) {
        m_byName.insert(m_messages.at(i).name, i);
        m_byId.insert(m_messages.at(i).id | (m_messages.at(i).extended ? 0x80000000u : 0), i);
    }
    if (m_messages.isEmpty()) {
        if (error) *error = QStringLiteral("no messages found");
        return false;
    }
    return true;
}

const Dbc::Message *Dbc::message(const QString &name) const
{
    const auto it = m_byName.constFind(name);
    return it == m_byName.cend() ? nullptr : &m_messages.at(*it);
}

const Dbc::Message *Dbc::message(quint32 id) const
{
    const auto it = m_byId.constFind(id);
    return it == m_byId.cend() ? nullptr : &m_messages.at(*it);
}

bool Dbc::has(const QString &message, const QStringList &signalNames) const
{
    const Message *m = this->message(message);
    if (!m)
        return false;
    for (const QString &s : signalNames)
        if (!m->signal(s))
            return false;
    return true;
}

quint64 Dbc::rawGet(const Signal &s, const uint8_t *data, int len, bool *ok)
{
    quint64 v = 0;
    for (int pos : bitPositions(s)) {
        if (pos / 8 >= len) {
            if (ok) *ok = false;
            return 0;
        }
        v = (v << 1) | quint64(bitAt(data, pos));
    }
    if (ok) *ok = true;
    return v;
}

void Dbc::rawPut(const Signal &s, uint8_t *data, int len, quint64 raw)
{
    const QList<int> bits = bitPositions(s);
    for (int i = 0; i < bits.size(); ++i) {
        const int pos = bits.at(i);
        if (pos / 8 < len)
            setBit(data, pos, int((raw >> (bits.size() - 1 - i)) & 1));
    }
}

QHash<QString, double> Dbc::decode(const Message &m, const uint8_t *data, int len)
{
    QHash<QString, double> out;
    for (const Signal &s : m.signals_) {
        bool ok = false;
        quint64 raw = rawGet(s, data, len, &ok);
        if (!ok)
            continue;
        double v;
        if (s.isSigned && s.length < 64 && (raw >> (s.length - 1)) & 1)
            v = double(qint64(raw | (~quint64(0) << s.length)));
        else if (s.isSigned)
            v = double(qint64(raw));
        else
            v = double(raw);
        out.insert(s.name, v * s.scale + s.offset);
    }
    return out;
}

QByteArray Dbc::encode(const Message &m, const QHash<QString, double> &values)
{
    QByteArray out(m.dlc, '\0');
    auto *data = reinterpret_cast<uint8_t *>(out.data());
    for (const Signal &s : m.signals_) {
        const double phys = values.value(s.name, 0.0);
        double raw = std::round((phys - s.offset) / s.scale);
        // What the bits can hold
        const double lo = s.isSigned ? -std::ldexp(1.0, s.length - 1) : 0.0;
        const double hi = s.isSigned ? std::ldexp(1.0, s.length - 1) - 1 : std::ldexp(1.0, s.length) - 1;
        raw = std::clamp(raw, lo, hi);
        quint64 bits = s.isSigned ? quint64(qint64(raw)) : quint64(raw);
        if (s.length < 64)
            bits &= (quint64(1) << s.length) - 1;
        rawPut(s, data, m.dlc, bits);
    }
    return out;
}
