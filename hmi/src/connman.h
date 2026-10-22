#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusObjectPath>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

class ConnmanAgent;

// Wi-Fi through ConnMan (net.connman on the system bus).
//
// Used on the head unit (eglfs) only; elsewhere, or when ConnMan is not
// running, available is false and the HMI shows its demo networks.
// GODWIT_CONNMAN_BUS=system uses ConnMan anyway; =session talks to a ConnMan
// on the session bus, for testing.
class Connman : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Net)
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool wifiPowered READ wifiPowered NOTIFY wifiChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // [{ssid, sig (0..1), sec, saved, connected, autoConnect, state, path}], strongest first
    Q_PROPERTY(QVariantList networks READ networks NOTIFY networksChanged)
    Q_PROPERTY(QString connectedSsid READ connectedSsid NOTIFY networksChanged)
    Q_PROPERTY(QString connecting READ connecting NOTIFY connectingChanged)

public:
    explicit Connman(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool wifiPowered() const { return m_powered; }
    bool scanning() const { return m_scanning; }
    QVariantList networks() const { return m_networks; }
    QString connectedSsid() const;
    QString connecting() const { return m_connectingSsid; }

    Q_INVOKABLE void setWifiPowered(bool on);
    Q_INVOKABLE void scan();
    // passphrase is used if ConnMan asks for one (a secured network not yet saved)
    Q_INVOKABLE void connectTo(const QString &ssid, const QString &passphrase = QString());
    Q_INVOKABLE void disconnectFrom(const QString &ssid);
    Q_INVOKABLE void forget(const QString &ssid);
    Q_INVOKABLE void setAutoConnect(const QString &ssid, bool on);

    // Called by the agent
    QString takePassphrase();
    void agentError(const QString &error);

signals:
    void availableChanged();
    void wifiChanged();
    void scanningChanged();
    void networksChanged();
    void connectingChanged();
    void connected(const QString &ssid);
    void failed(const QString &ssid, const QString &reason);

private slots:
    void onServicesChanged(const QDBusMessage &msg);
    void onTechnologyChanged(const QString &name, const QDBusVariant &value);
    void onServiceChanged(const QDBusMessage &msg);

private:
    void start();
    void attach();
    void detach();
    void refreshServices();
    void refreshTechnology();
    void rebuild();
    QString pathFor(const QString &ssid) const;
    void setConnecting(const QString &ssid);

    QDBusConnection m_bus;
    int m_busTries = 0;
    bool m_available = false;
    bool m_powered = false;
    bool m_scanning = false;
    QString m_techPath;
    // service path -> properties, in ConnMan's order
    QList<QPair<QString, QVariantMap>> m_services;
    QVariantList m_networks;
    QString m_connectingSsid;
    QString m_pendingPassphrase;
    ConnmanAgent *m_agent = nullptr;
};

// net.connman.Agent: ConnMan calls this for a secured network's passphrase
class ConnmanAgent : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.Agent")

public:
    ConnmanAgent(QObject *object, Connman *owner);

public slots:
    void Release() {}
    void Cancel() {}
    void ReportError(const QDBusObjectPath &service, const QString &error);
    QVariantMap RequestInput(const QDBusObjectPath &service, const QVariantMap &fields);

private:
    Connman *m_owner;
};
