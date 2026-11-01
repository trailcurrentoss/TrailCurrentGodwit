#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

// The connected phone's contacts and call history, read over Bluetooth PBAP
// through obexd (org.bluez.obex, run on the system bus for the HMI by
// godwit-obexd.service). The phone asks its owner once to allow it.
//
// Used on the head unit (eglfs) only; elsewhere available is false and the
// HMI shows its demo contacts. GODWIT_OBEX_BUS=system or =session overrides.
class Phonebook : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // [{n, num, ini, adr}], by name; adr is the home (or first) address, or empty
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY contactsChanged)
    // [{n, num, ini, type: "in" | "out" | "missed", when: Date}], newest first
    Q_PROPERTY(QVariantList recents READ recents NOTIFY recentsChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)

public:
    explicit Phonebook(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QVariantList contacts() const { return m_contacts; }
    QVariantList recents() const { return m_recents; }
    bool syncing() const { return !m_session.isEmpty() || !m_steps.isEmpty(); }

    // Read the phone at this Bluetooth address; empty clears the lists.
    // With recentsOnly, or for the phone already read, only the call history
    // is read again.
    Q_INVOKABLE void sync(const QString &address, bool recentsOnly = false);
    // The contact's name for a phone number, or empty
    Q_INVOKABLE QString nameFor(const QString &number) const;
    // Read the connected phone's contacts and call history again. Phones send
    // no word of a contact added or changed, so this runs every 10 minutes,
    // and when asked: opening Phone, or a spoken name that is not found.
    Q_INVOKABLE void refresh();

signals:
    void availableChanged();
    void contactsChanged();
    void recentsChanged();
    void syncingChanged();
    // A read finished, whether or not anything changed
    void synced();

private slots:
    void onTransferChanged(const QDBusMessage &msg);

private:
    void start();
    void next();
    void finish(const QString &error = QString());
    void pull(const QString &book);
    void loaded(const QString &book, const QByteArray &vcards);

    QDBusConnection m_bus;
    int m_busTries = 0;
    bool m_available = false;
    QString m_address;
    QString m_session;
    QStringList m_steps;     // phonebooks still to read: "pb", "cch"
    QString m_book;          // the one being read
    QString m_transfer;
    QString m_file;
    bool m_failed = false;   // a phonebook could not be read this time
    int m_retries = 0;
    bool m_force = false;    // refresh(): read the contacts even if there are some
    QTimer m_refresh;
    QVariantList m_contacts;
    QVariantList m_recents;
    QHash<QString, QString> m_names; // every number a contact has -> name
};
