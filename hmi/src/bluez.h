#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QMap>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

class BluezAgent;

// Bluetooth through BlueZ (org.bluez on the system bus): power, the paired
// phones, and pairing a new one.
//
// Pairing is started from the phone, as in a car: startPairing() makes the
// head unit visible, the phone picks it, and both show a six-digit code to
// confirm. Power goes through ConnMan when it manages Bluetooth, since ConnMan
// also owns the radio's rfkill switch.
//
// Used on the head unit (eglfs) only; elsewhere available is false and the
// HMI shows its demo phones, so the simulator leaves a PC's Bluetooth alone.
// GODWIT_BLUEZ_BUS=system uses BlueZ anyway; =session uses the session bus,
// for testing.
class Bluez : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Bt)
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool powered READ powered NOTIFY poweredChanged)
    // The name phones see
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    // Paired phones: [{name, address, connected, model}]
    Q_PROPERTY(QVariantList phones READ phones NOTIFY phonesChanged)
    // {} when not pairing, else {stage: "search" | "confirm" | "busy", phone: {name}, code}
    Q_PROPERTY(QVariantMap pairing READ pairing NOTIFY pairingChanged)
    // The connected phone's media player (AVRCP): {} when there is none, else
    // {name, status: "playing" | "paused" | "stopped", title, artist, album,
    //  duration and position in ms, at: when position was read, ms since epoch}
    Q_PROPERTY(QVariantMap player READ player NOTIFY playerChanged)

public:
    explicit Bluez(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool powered() const;
    QString name() const;
    QVariantList phones() const { return m_phones; }
    QVariantMap pairing() const { return m_pairing; }
    QVariantMap player() const { return m_playerInfo; }

    Q_INVOKABLE void setPowered(bool on);
    Q_INVOKABLE void startPairing();
    Q_INVOKABLE void stopPairing();
    Q_INVOKABLE void acceptPairing();
    Q_INVOKABLE void connectPhone(const QString &address);
    Q_INVOKABLE void disconnectPhone(const QString &address);
    Q_INVOKABLE void forget(const QString &address);
    Q_INVOKABLE void play() { playerCall(QStringLiteral("Play")); }
    Q_INVOKABLE void pause() { playerCall(QStringLiteral("Pause")); }
    Q_INVOKABLE void next() { playerCall(QStringLiteral("Next")); }
    Q_INVOKABLE void previous() { playerCall(QStringLiteral("Previous")); }

    // Called by the agent
    bool pairingMode() const { return m_pairingMode; }
    bool isPaired(const QString &path) const;
    void confirmRequested(const QDBusMessage &msg, const QString &device, const QString &code);
    void codeShown(const QString &device, const QString &code);
    void agentCancelled();

signals:
    void availableChanged();
    void poweredChanged();
    void nameChanged();
    void phonesChanged();
    void pairingChanged();
    void playerChanged();
    void paired(const QString &name);
    void pairingFailed(const QString &reason);
    void connectFailed(const QString &name, const QString &reason);

private slots:
    void onInterfacesAdded(const QDBusMessage &msg);
    void onInterfacesRemoved(const QDBusMessage &msg);
    void onPropertiesChanged(const QDBusMessage &msg);

private:
    void start();
    void attach();
    void detach();
    void refresh();
    void rebuild();
    void setAdapterProperty(const QString &name, const QVariant &value);
    void setDeviceProperty(const QString &path, const QString &name, const QVariant &value);
    void setPairing(const QVariantMap &p);
    void replyToPending(bool accept);
    void reconnect();
    void pagePhone(const QString &path);
    void ensureMedia(const QString &path);
    void playerCall(const QString &method);
    void setPlayer(const QString &path, const QVariantMap &props);
    void rebuildPlayer();
    QString pathFor(const QString &address) const;
    QString deviceName(const QString &path) const;

    QDBusConnection m_bus;
    int m_busTries = 0;
    bool m_available = false;
    QString m_adapter;
    QVariantMap m_adapterProps;
    QMap<QString, QVariantMap> m_devices; // Device1 path -> properties
    QVariantList m_phones;
    QVariantMap m_pairing;
    QString m_player;                     // MediaPlayer1 path
    QVariantMap m_playerProps;
    QVariantMap m_playerInfo;
    QSet<QString> m_transports;           // MediaTransport1 paths
    QSet<QString> m_connecting;           // phones being paged now
    QSet<QString> m_declined;             // phones that refused a page
    bool m_pairingMode = false;
    QDBusMessage m_pending;               // an unanswered RequestConfirmation
    QString m_pendingDevice;
    BluezAgent *m_agent = nullptr;
};

// org.bluez.Agent1: BlueZ calls this while a phone pairs
class BluezAgent : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    explicit BluezAgent(Bluez *owner);

public slots:
    void Release() {}
    QString RequestPinCode(const QDBusObjectPath &device);
    void DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
    uint RequestPasskey(const QDBusObjectPath &device);
    void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath &device);
    void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    void Cancel();

private:
    Bluez *m_owner;
};
