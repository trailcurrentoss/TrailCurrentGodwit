#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <cstdint>

// The messages of a DBC file: enough of it to decode a classic CAN frame's
// signals and to encode one from them. Reads BO_ (message) and SG_ (signal)
// lines and nothing else: no multiplexing, value tables or attributes.
//
// Code names messages and signals ("TorrentStatus0", "Output3Value"), so the
// byte layout and scaling of each one is only ever written in the DBC.
class Dbc
{
public:
    struct Signal {
        QString name;
        int start = 0;          // the DBC's start bit: the MSB if big-endian, the LSB if little-endian
        int length = 0;
        bool bigEndian = false; // @0 (Motorola) rather than @1 (Intel)
        bool isSigned = false;
        double scale = 1;
        double offset = 0;
        double min = 0;
        double max = 0;
    };
    struct Message {
        quint32 id = 0;
        bool extended = false;
        QString name;
        int dlc = 0;
        QList<Signal> signals_;
        const Signal *signal(const QString &name) const;
    };

    // Replaces anything loaded before. False, with a reason, if the file cannot
    // be read or has no messages; lines that do not parse are skipped.
    bool load(const QString &path, QString *error = nullptr);
    bool parse(const QByteArray &text, QString *error = nullptr);

    bool isEmpty() const { return m_messages.isEmpty(); }
    const Message *message(const QString &name) const;
    const Message *message(quint32 id) const;
    // Whether the message exists with all of these signals
    bool has(const QString &message, const QStringList &signalNames) const;

    // Physical values (raw * scale + offset) of every signal the frame's data
    // covers. Signals past the end of a short frame are left out.
    static QHash<QString, double> decode(const Message &m, const uint8_t *data, int len);
    // A frame's data for the message, dlc bytes long. Signals not given are 0;
    // values are rounded to a step of the signal and clamped to what fits.
    static QByteArray encode(const Message &m, const QHash<QString, double> &values);

    static quint64 rawGet(const Signal &s, const uint8_t *data, int len, bool *ok = nullptr);
    static void rawPut(const Signal &s, uint8_t *data, int len, quint64 raw);

private:
    QList<Message> m_messages;
    QHash<QString, int> m_byName;
    QHash<quint32, int> m_byId;
};
