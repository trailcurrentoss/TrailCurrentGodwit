#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

// Text messages arriving on the connected phone, over Bluetooth MAP through
// obexd (org.bluez.obex, the same daemon Phonebook uses). A session stays open
// while the phone is connected; obexd registers it for the phone's new-message
// notifications, and each new inbox message is reported with its sender. Its
// text is downloaded only when asked for, as reading it marks it read on the
// phone. iPhones send notifications only with Show Notifications on for the
// head unit in their Bluetooth settings.
//
// Used on the head unit (eglfs) only; elsewhere available is false and the
// simulator's demo message stands in. GODWIT_OBEX_BUS=system or =session
// overrides.
class Messages : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // A session is open and notifications are on
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit Messages(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool connected() const { return !m_session.isEmpty(); }

    // Take the messages of the phone at this Bluetooth address; empty stops
    Q_INVOKABLE void connectPhone(const QString &address);
    // Download a received message's text; answered by textReady or textFailed.
    // A new fetch replaces one still downloading.
    Q_INVOKABLE void fetch(const QString &id);
    // Text a reply to a number; answered by sent or sendFailed
    Q_INVOKABLE void send(const QString &number, const QString &text);

signals:
    void availableChanged();
    void connectedChanged();
    // msg: {id, n, num, subject}; n and subject may be empty
    void received(const QVariantMap &msg);
    void textReady(const QString &id, const QString &name, const QString &number, const QString &text);
    void textFailed(const QString &id);
    void sent(const QString &number);
    void sendFailed(const QString &number);

private slots:
    void onInterfacesAdded(const QDBusMessage &msg);
    void onInterfacesRemoved(const QDBusMessage &msg);
    void onTransferChanged(const QDBusMessage &msg);

private:
    void start();
    void open();
    void close();
    void retry(const QString &error);
    void lookUp(const QStringList &ids);
    void fetched(const QByteArray &bmsg);
    void endFetch(bool ok);
    void endSend(bool ok);
    void markRead(const QString &id);

    QDBusConnection m_bus;
    int m_busTries = 0;
    bool m_available = false;
    QString m_address;
    bool m_opening = false;
    QString m_session;
    int m_retries = 0;
    // Message handles already reported for this phone. An iPhone notifies a
    // new message together with the conversation's earlier ones, and again
    // for its recent messages each time a session opens.
    QSet<QString> m_seen;
    QString m_newest;        // the latest reported message's timestamp
    QElapsedTimer m_opened;
    QStringList m_batch;     // notified together, looked up together
    QTimer m_batchTimer;
    QString m_fetchId;
    QString m_transfer;
    QString m_file;
    QString m_sendTo;
    QString m_sendTransfer;
    QString m_sendFile;
};
