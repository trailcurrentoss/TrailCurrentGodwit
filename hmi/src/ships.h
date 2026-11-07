#pragma once

#include "tunerreceiver.h"

// Ships nearby, by AIS: godwit-ais decodes them. A ship sends its own name, type
// and destination, so nothing needs looking up.
//
// Each of list is { mmsi, name, destination, type, lat, lon, speed, course, at },
// at being when the position was heard, in ms since the epoch.
//
// GODWIT_AIS_BIN overrides where godwit-ais is.
class Ships : public TunerReceiver
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Ships(QObject *parent = nullptr);

protected:
    bool handleLine(const QByteArray &line) override;
};
