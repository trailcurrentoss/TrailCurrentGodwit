#pragma once

#include "tunerreceiver.h"

class QFile;

// One table of the aircraft data: sorted lines of "key TAB value". Mapped rather
// than loaded, as the routes alone are several megabytes.
class AircraftTable
{
public:
    ~AircraftTable();
    bool open(const QString &path);
    // The value for a key, or empty
    QByteArray find(const QByteArray &key) const;

private:
    QFile *m_file = nullptr;
    const char *m_data = nullptr;
    qint64 m_size = 0;
};

// Aircraft nearby, by ADS-B: godwit-adsb decodes them. What each is and where it
// is going come from its broadcast's address and callsign, looked up in tables on
// the head unit (radio/tools/pack_aircraft_data.py). No internet is involved.
//
// Each of list is { hex, call, type, reg, route: [..airports], category, lat, lon,
// alt, speed, at }, at being when the position was heard, in ms since the epoch.
//
// GODWIT_ADSB_BIN overrides where godwit-adsb is and GODWIT_AIRCRAFT_DATA where the
// tables are.
class Aircraft : public TunerReceiver
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Aircraft(QObject *parent = nullptr);

protected:
    bool handleLine(const QByteArray &line) override;

private:
    QVariantMap describe(const QByteArray &hex, const QByteArray &call);

    AircraftTable m_types, m_routes;
};
