#include "ships.h"

#include <QDateTime>

Ships::Ships(QObject *parent)
    : TunerReceiver("ships", "GODWIT_AIS_BIN", QStringLiteral("/usr/bin/godwit-ais"), parent)
{
}

bool Ships::handleLine(const QByteArray &line)
{
    // Tabs, as names have spaces in them
    const QList<QByteArray> p = line.split('\t');
    if (p.size() != 10 || p[0] != "ship")
        return false;
    const auto given = [](const QByteArray &v) { return v != "-"; };

    QVariantMap s;
    s.insert(QStringLiteral("mmsi"), QString::fromLatin1(p[1]));
    if (given(p[2]) && given(p[3])) {
        s.insert(QStringLiteral("lat"), p[2].toDouble());
        s.insert(QStringLiteral("lon"), p[3].toDouble());
        s.insert(QStringLiteral("at"), QDateTime::currentMSecsSinceEpoch() - p[7].toLongLong() * 1000);
    }
    if (given(p[4]))
        s.insert(QStringLiteral("speed"), p[4].toDouble());
    if (given(p[5]))
        s.insert(QStringLiteral("course"), p[5].toDouble());
    if (given(p[6]))
        s.insert(QStringLiteral("type"), p[6].toInt());
    if (!p[8].isEmpty())
        s.insert(QStringLiteral("name"), QString::fromLatin1(p[8]));
    if (!p[9].isEmpty())
        s.insert(QStringLiteral("destination"), QString::fromLatin1(p[9]));
    m_items.insert(p[1], s);
    return true;
}
