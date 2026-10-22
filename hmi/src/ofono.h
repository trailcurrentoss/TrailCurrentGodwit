#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QMap>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

// Phone calls through oFono (org.ofono on the system bus). A paired phone
// connected for hands-free shows up in oFono as an "hfp" modem; calls made or
// received on it appear here, and can be answered, dialled and ended from the
// head unit. Call audio is routed separately (PipeWire).
//
// Used on the head unit (eglfs) only; elsewhere available is false and the
// HMI shows its demo calls. GODWIT_OFONO_BUS=system or =session overrides.
class Ofono : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Calls)
    QML_SINGLETON

    // A phone is connected for calls
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // The phone's name and Bluetooth address
    Q_PROPERTY(QString phoneName READ phoneName NOTIFY availableChanged)
    Q_PROPERTY(QString address READ address NOTIFY availableChanged)
    // [{path, number, name, state}], state as oFono gives it: incoming,
    // waiting, dialing, alerting, active, held
    Q_PROPERTY(QVariantList calls READ calls NOTIFY callsChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)

public:
    explicit Ofono(QObject *parent = nullptr);

    bool available() const { return !m_modem.isEmpty(); }
    QString phoneName() const;
    QString address() const;
    QVariantList calls() const { return m_callList; }
    bool muted() const { return m_muted; }

    Q_INVOKABLE void dial(const QString &number);
    Q_INVOKABLE void answer();
    // Ends the current call; with none active, declines a ringing one
    Q_INVOKABLE void hangup();
    Q_INVOKABLE void setMuted(bool on);

signals:
    void availableChanged();
    void callsChanged();
    void mutedChanged();
    void dialFailed(const QString &number, const QString &reason);

private slots:
    void onModemAdded(const QDBusMessage &msg);
    void onModemRemoved(const QDBusMessage &msg);
    void onModemChanged(const QDBusMessage &msg);
    void onCallAdded(const QDBusMessage &msg);
    void onCallRemoved(const QDBusMessage &msg);
    void onCallChanged(const QDBusMessage &msg);
    void onVolumeChanged(const QDBusMessage &msg);

private:
    void start();
    void refreshModems();
    void pickModem();
    void refreshCalls();
    void rebuild();
    QString callIn(const QStringList &states) const;

    QDBusConnection m_bus;
    int m_busTries = 0;
    QMap<QString, QVariantMap> m_modems; // path -> properties
    QString m_modem;                     // the one in use
    QMap<QString, QVariantMap> m_calls;  // call path -> properties
    QVariantList m_callList;
    bool m_muted = false;
};
