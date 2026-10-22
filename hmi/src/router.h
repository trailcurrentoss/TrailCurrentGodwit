#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QThread>
#include <QVariantMap>

class RouterWorker;

// Routes and turn-by-turn steps from Valhalla, linked into the HMI and read
// from the installed map bundle's valhalla_tiles.tar. It runs on its own
// thread; nothing here reaches the network.
//
// route, once planned:
//   { mi, mins, steps: [{dir, st, d, t, i}], shape: [[lon, lat], ...],
//     lat, lon }   (lat, lon: the destination it was planned to)
// steps are the maneuvers after leaving: dir is a DemoData.DIRS key, st the
// road or place, d the miles to it from the step before, t the minutes, i
// Valhalla's instruction. The last step is the arrival.
//
// GODWIT_VALHALLA_CONFIG overrides /etc/valhalla/valhalla.json. Builds
// without Valhalla (the simulator) have available false.
class Router : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap route READ route NOTIFY routeChanged)
    Q_PROPERTY(QString error READ error NOTIFY routeChanged)

public:
    explicit Router(QObject *parent = nullptr);
    ~Router() override;

    bool available() const { return m_available; }
    bool busy() const { return m_busy; }
    QVariantMap route() const { return m_route; }
    QString error() const { return m_error; }

    Q_INVOKABLE void plan(double fromLat, double fromLon, double toLat, double toLon);
    Q_INVOKABLE void clear();

signals:
    void busyChanged();
    void routeChanged();
    // to the worker
    void request(int id, const QString &json);

private:
    void finished(int id, const QVariantMap &route, const QString &error);

    bool m_available = false;
    bool m_busy = false;
    int m_id = 0;
    QVariantMap m_route;
    QString m_error;
    QThread m_thread;
    RouterWorker *m_worker = nullptr;
};
