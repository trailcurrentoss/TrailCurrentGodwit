#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

// Destination search through Photon, which serves the installed map bundle's
// search index on the head unit itself (photon.service, 127.0.0.1:2322).
// Nothing here leaves the machine.
//
// results are rows for the search list: {n, a, icon, mi, lat, lon}, Photon's
// order, which already favours places near the vehicle.
//
// GODWIT_PHOTON_URL overrides the server, e.g. for the simulator.
class Places : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // false until Photon has answered; it starts after the HMI
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)

public:
    explicit Places(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool busy() const { return !m_reply.isNull(); }
    QVariantList results() const { return m_results; }

    // Near (lat, lon). Typing is coalesced; an empty query clears the results.
    Q_INVOKABLE void search(const QString &query, double lat, double lon);

signals:
    void availableChanged();
    void busyChanged();
    void resultsChanged();

private:
    void send();
    void finished(QNetworkReply *reply);
    void setAvailable(bool available);

    QString m_base;
    QString m_query;
    double m_lat = 0;
    double m_lon = 0;
    bool m_available = false;
    QVariantList m_results;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
};
