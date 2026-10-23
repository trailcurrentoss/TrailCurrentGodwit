#include "messages.h"

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcMsg, "godwit.messages")

namespace {

const QString Service = QStringLiteral("org.bluez.obex");
const QString ClientIface = QStringLiteral("org.bluez.obex.Client1");
const QString MapIface = QStringLiteral("org.bluez.obex.MessageAccess1");
const QString MessageIface = QStringLiteral("org.bluez.obex.Message1");
const QString SessionIface = QStringLiteral("org.bluez.obex.Session1");
const QString TransferIface = QStringLiteral("org.bluez.obex.Transfer1");
const QString PropsIface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString ObjectsIface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString BusName = QStringLiteral("godwit-map");
// Inbox messages listed to find a new one's sender
constexpr quint16 ListCount = 30;
// Notifications this soon after a session opens are the phone's recent
// messages, not new ones
constexpr int ReplayMs = 5000;

// A private connection, retried until the system bus is up (see connman.cpp)
QDBusConnection openBus()
{
    QDBusConnection::disconnectFromBus(BusName);
    return QDBusConnection::connectToBus(
        qEnvironmentVariable("GODWIT_OBEX_BUS") == QLatin1String("session")
            ? QDBusConnection::SessionBus : QDBusConnection::SystemBus,
        BusName);
}

// ---------------------------------------------------------------- bMessage

struct Bmsg
{
    QString name;
    QString number;
    QString body;
};

// The text/plain part of an MMS body, or the body as it is
QString plainText(const QString &body)
{
    if (!body.contains(QLatin1String("Content-Type:"), Qt::CaseInsensitive))
        return body;
    const QStringList lines = body.split(QLatin1Char('\n'));
    QStringList out;
    bool plain = false, inBody = false;
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("--"))) {
            plain = inBody = false;
            continue;
        }
        if (!inBody) {
            if (line.startsWith(QLatin1String("Content-Type:"), Qt::CaseInsensitive))
                plain = line.contains(QLatin1String("text/plain"), Qt::CaseInsensitive);
            else if (line.trimmed().isEmpty())
                inBody = plain;
            continue;
        }
        out.append(line);
    }
    return out.isEmpty() ? body : out.join(QLatin1Char('\n'));
}

// The sender (the vCard before the envelope) and the text of a bMessage
Bmsg parseBmsg(const QByteArray &data)
{
    QString text = QString::fromUtf8(data);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    Bmsg m;
    QString n;
    QStringList body;
    bool inCard = false, inEnv = false, inMsg = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (inMsg) {
            if (line == QLatin1String("END:MSG"))
                inMsg = false;
            else
                body.append(line);
            continue;
        }
        if (line == QLatin1String("BEGIN:MSG")) {
            inMsg = true;
            continue;
        }
        if (line == QLatin1String("BEGIN:BENV"))
            inEnv = true;
        if (inEnv)
            continue;
        if (line == QLatin1String("BEGIN:VCARD")) {
            inCard = true;
            continue;
        }
        if (line == QLatin1String("END:VCARD")) {
            inCard = false;
            continue;
        }
        if (!inCard)
            continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).section(QLatin1Char(';'), 0, 0).toUpper();
        const QString value = line.mid(colon + 1).trimmed();
        if (key == QLatin1String("FN") && !value.isEmpty())
            m.name = value;
        else if (key == QLatin1String("N"))
            n = QStringList { value.section(QLatin1Char(';'), 1, 1), value.section(QLatin1Char(';'), 0, 0) }
                    .join(QLatin1Char(' ')).simplified();
        else if (key == QLatin1String("TEL") && m.number.isEmpty())
            m.number = value;
    }
    if (m.name.isEmpty())
        m.name = n;
    m.body = plainText(body.join(QLatin1Char('\n'))).trimmed();
    return m;
}

// A text to one number, as the phone's outbox takes it
QByteArray composeBmsg(const QString &number, const QString &text)
{
    const QByteArray body = "BEGIN:MSG\r\n" + text.toUtf8() + "\r\nEND:MSG\r\n";
    return "BEGIN:BMSG\r\nVERSION:1.0\r\nSTATUS:READ\r\nTYPE:SMS_GSM\r\nFOLDER:TELECOM/MSG/OUTBOX\r\n"
           "BEGIN:BENV\r\nBEGIN:VCARD\r\nVERSION:2.1\r\nN:\r\nTEL:" + number.toUtf8() + "\r\nEND:VCARD\r\n"
           "BEGIN:BBODY\r\nCHARSET:UTF-8\r\nLENGTH:" + QByteArray::number(body.size()) + "\r\n"
           + body + "END:BBODY\r\nEND:BENV\r\nEND:BMSG\r\n";
}

} // namespace

Messages::Messages(QObject *parent)
    : QObject(parent)
    , m_bus(openBus())
{
    m_batchTimer.setSingleShot(true);
    m_batchTimer.setInterval(400);
    connect(&m_batchTimer, &QTimer::timeout, this, [this] {
        const QStringList ids = m_batch;
        m_batch.clear();
        if (!ids.isEmpty() && connected())
            lookUp(ids);
    });
    // Only the head unit reads real phones, as for Phonebook
    if (qEnvironmentVariableIsEmpty("GODWIT_OBEX_BUS") && QGuiApplication::platformName() != QLatin1String("eglfs"))
        return;
    start();
}

void Messages::start()
{
    if (!m_bus.isConnected()) {
        if (++m_busTries < 120)
            QTimer::singleShot(500, this, [this] { m_bus = openBus(); start(); });
        return;
    }
    m_available = true;
    emit availableChanged();
    m_bus.connect(Service, QStringLiteral("/"), ObjectsIface, QStringLiteral("InterfacesAdded"),
                  this, SLOT(onInterfacesAdded(QDBusMessage)));
    m_bus.connect(Service, QStringLiteral("/"), ObjectsIface, QStringLiteral("InterfacesRemoved"),
                  this, SLOT(onInterfacesRemoved(QDBusMessage)));
    m_bus.connect(Service, QString(), PropsIface, QStringLiteral("PropertiesChanged"),
                  this, SLOT(onTransferChanged(QDBusMessage)));
    // obexd starting after the phone connected, or again after it stopped
    auto *watcher = new QDBusServiceWatcher(Service, m_bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        if (!m_address.isEmpty() && !connected() && !m_opening)
            open();
    });
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        m_opening = false;
        if (connected()) {
            m_session.clear(); // gone with obexd
            emit connectedChanged();
        }
        endFetch(false);
        endSend(false);
    });
}

void Messages::connectPhone(const QString &address)
{
    if (!m_available)
        return;
    // The same phone again: try once more if it refused before, as it may
    // allow it now
    if (address == m_address) {
        if (!address.isEmpty() && !connected() && !m_opening) {
            m_retries = 0;
            open();
        }
        return;
    }
    close();
    m_address = address;
    m_retries = 0;
    m_seen.clear();
    m_newest.clear();
    if (!address.isEmpty())
        open();
}

void Messages::open()
{
    const QString address = m_address;
    m_opening = true;
    qCInfo(lcMsg) << "opening messages on" << address;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez/obex"), ClientIface, QStringLiteral("CreateSession"));
    call << address << QVariantMap { { QStringLiteral("Target"), QStringLiteral("map") } };
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, address](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (address != m_address || !m_opening)
            return;
        m_opening = false;
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            // The phone may not allow it yet, or may still be busy connecting
            retry(reply.errorName() + QLatin1Char(' ') + reply.errorMessage());
            return;
        }
        const QString session = reply.arguments().constFirst().value<QDBusObjectPath>().path();
        // New messages are listed from here
        QDBusMessage cd = QDBusMessage::createMethodCall(Service, session, MapIface, QStringLiteral("SetFolder"));
        cd << QStringLiteral("/telecom/msg");
        auto *cw = new QDBusPendingCallWatcher(m_bus.asyncCall(cd), this);
        connect(cw, &QDBusPendingCallWatcher::finished, this, [this, address, session](QDBusPendingCallWatcher *cw) {
            cw->deleteLater();
            if (address != m_address) {
                QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez/obex"), ClientIface, QStringLiteral("RemoveSession"));
                call << QVariant::fromValue(QDBusObjectPath(session));
                m_bus.asyncCall(call);
                return;
            }
            if (cw->isError())
                qCWarning(lcMsg) << "cannot open the message folders:" << cw->error().message();
            qCInfo(lcMsg) << "taking messages from" << address;
            m_session = session;
            m_opened.start();
            m_retries = 0;
            emit connectedChanged();
            // The newest message already on the phone; the phone re-sends
            // earlier ones now and then, and none of them is new
            QDBusMessage ls = QDBusMessage::createMethodCall(Service, session, MapIface, QStringLiteral("ListMessages"));
            ls << QStringLiteral("inbox") << QVariantMap { { QStringLiteral("MaxCount"), QVariant::fromValue(quint16(1)) } };
            auto *lw = new QDBusPendingCallWatcher(m_bus.asyncCall(ls), this);
            connect(lw, &QDBusPendingCallWatcher::finished, this, [this, session](QDBusPendingCallWatcher *lw) {
                lw->deleteLater();
                const QDBusMessage reply = lw->reply();
                if (session != m_session || reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
                    return;
                const QDBusArgument list = reply.arguments().constFirst().value<QDBusArgument>();
                list.beginMap();
                while (!list.atEnd()) {
                    QDBusObjectPath path;
                    QVariantMap p;
                    list.beginMapEntry();
                    list >> path >> p;
                    list.endMapEntry();
                    m_newest = std::max(m_newest, p.value(QStringLiteral("Timestamp")).toString());
                }
                list.endMap();
                qCDebug(lcMsg) << "newest on the phone" << m_newest;
            });
        });
    });
}

void Messages::retry(const QString &error)
{
    qCWarning(lcMsg) << "messages not available:" << error;
    if (m_address.isEmpty() || m_retries >= 3)
        return;
    ++m_retries;
    const QString address = m_address;
    QTimer::singleShot(30000, this, [this, address] {
        if (address == m_address && !connected() && !m_opening)
            open();
    });
}

void Messages::close()
{
    m_opening = false;
    endFetch(false);
    endSend(false);
    if (m_session.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez/obex"), ClientIface, QStringLiteral("RemoveSession"));
    call << QVariant::fromValue(QDBusObjectPath(m_session));
    m_bus.asyncCall(call);
    m_session.clear();
    emit connectedChanged();
}

// A new message from the phone's notification, with only its folder and type
void Messages::onInterfacesAdded(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (m_session.isEmpty() || args.size() < 2)
        return;
    const QString path = args.at(0).value<QDBusObjectPath>().path();
    if (!path.startsWith(m_session + QStringLiteral("/message")))
        return;
    QVariantMap props;
    bool isMessage = false;
    const QDBusArgument ifaces = args.at(1).value<QDBusArgument>();
    ifaces.beginMap();
    while (!ifaces.atEnd()) {
        QString iface;
        QVariantMap p;
        ifaces.beginMapEntry();
        ifaces >> iface >> p;
        ifaces.endMapEntry();
        if (iface == MessageIface) {
            isMessage = true;
            props = p;
        }
    }
    ifaces.endMap();
    // Sent messages and other folders are reported too
    if (!isMessage || !props.value(QStringLiteral("Folder")).toString().contains(QLatin1String("inbox"), Qt::CaseInsensitive))
        return;
    // The handle, the same in every session
    const QString handle = path.mid(m_session.size() + 8);
    if (m_seen.contains(handle))
        return;
    m_seen.insert(handle);
    if (m_opened.elapsed() < ReplayMs) {
        qCDebug(lcMsg) << "earlier message" << path;
        return;
    }
    qCInfo(lcMsg) << "new message" << path << props.value(QStringLiteral("Type")).toString();
    m_batch.append(path);
    m_batchTimer.start();
}

void Messages::onInterfacesRemoved(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (m_session.isEmpty() || args.size() < 2 || args.at(0).value<QDBusObjectPath>().path() != m_session)
        return;
    if (!args.at(1).toStringList().contains(SessionIface))
        return;
    // The phone disconnected; it is taken again when it reconnects
    qCInfo(lcMsg) << "messages closed by the phone";
    m_session.clear();
    m_address.clear();
    endFetch(false);
    endSend(false);
    emit connectedChanged();
}

// The notification carries no sender: list the inbox to find it. Of messages
// notified together, only the newest is reported.
void Messages::lookUp(const QStringList &ids)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_session, MapIface, QStringLiteral("ListMessages"));
    call << QStringLiteral("inbox")
         << QVariantMap { { QStringLiteral("MaxCount"), QVariant::fromValue(ListCount) } };
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, ids](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        // The phone lists the new message first; the newest timestamp wins
        QString id = ids.first();
        QVariantMap found;
        const QDBusMessage reply = w->reply();
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QDBusArgument list = reply.arguments().constFirst().value<QDBusArgument>();
            list.beginMap();
            while (!list.atEnd()) {
                QDBusObjectPath path;
                QVariantMap p;
                list.beginMapEntry();
                list >> path >> p;
                list.endMapEntry();
                // "YYYYMMDDTHHMMSS" sorts as time does
                if (ids.contains(path.path())
                    && (found.isEmpty() || p.value(QStringLiteral("Timestamp")).toString() > found.value(QStringLiteral("Timestamp")).toString())) {
                    id = path.path();
                    found = p;
                }
            }
            list.endMap();
        } else {
            qCWarning(lcMsg) << "cannot list the inbox:" << reply.errorMessage();
        }
        // The phone also re-sends a conversation's earlier messages, sometimes
        // a moment after the new one: report nothing older than the last
        const QString stamp = found.value(QStringLiteral("Timestamp")).toString();
        if (found.value(QStringLiteral("Read")).toBool()) {
            qCDebug(lcMsg) << "already read" << id;
            return;
        }
        if (!stamp.isEmpty()) {
            if (stamp <= m_newest) {
                qCDebug(lcMsg) << "older message" << id;
                return;
            }
            m_newest = stamp;
        }
        // Reported even without its sender, so it can still be read
        emit received({ { QStringLiteral("id"), id },
                        { QStringLiteral("n"), found.value(QStringLiteral("Sender")).toString() },
                        { QStringLiteral("num"), found.value(QStringLiteral("SenderAddress")).toString() },
                        { QStringLiteral("subject"), found.value(QStringLiteral("Subject")).toString() } });
    });
}

void Messages::fetch(const QString &id)
{
    if (!connected() || !id.startsWith(m_session)) {
        emit textFailed(id);
        return;
    }
    endFetch(false);
    m_fetchId = id;
    const QString dir = qEnvironmentVariable("XDG_CACHE_HOME", QDir::tempPath());
    m_file = dir + QStringLiteral("/godwit-message.bmsg");
    QFile::remove(m_file);
    QDBusMessage call = QDBusMessage::createMethodCall(Service, id, MessageIface, QStringLiteral("Get"));
    call << m_file << false;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, id](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (m_fetchId != id)
            return;
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcMsg) << "cannot read" << id << ":" << reply.errorMessage();
            endFetch(false);
            return;
        }
        m_transfer = reply.arguments().constFirst().value<QDBusObjectPath>().path();
        const QString transfer = m_transfer;
        QTimer::singleShot(30000, this, [this, transfer] {
            if (m_transfer == transfer) {
                qCWarning(lcMsg) << "reading the message timed out";
                endFetch(false);
            }
        });
    });
}

void Messages::send(const QString &number, const QString &text)
{
    if (!connected() || number.isEmpty() || text.trimmed().isEmpty()) {
        emit sendFailed(number);
        return;
    }
    endSend(false);
    m_sendTo = number;
    m_sendFile = qEnvironmentVariable("XDG_CACHE_HOME", QDir::tempPath()) + QStringLiteral("/godwit-reply.bmsg");
    QFile f(m_sendFile);
    if (!f.open(QIODevice::WriteOnly) || f.write(composeBmsg(number, text.trimmed())) < 0) {
        qCWarning(lcMsg) << "cannot write" << m_sendFile;
        endSend(false);
        return;
    }
    f.close();
    qCInfo(lcMsg) << "sending a reply to" << number;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_session, MapIface, QStringLiteral("PushMessage"));
    call << m_sendFile << QStringLiteral("outbox") << QVariantMap { { QStringLiteral("Charset"), QStringLiteral("utf8") } };
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, number](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (m_sendTo != number)
            return;
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcMsg) << "cannot send:" << reply.errorName() << reply.errorMessage();
            endSend(false);
            return;
        }
        m_sendTransfer = reply.arguments().constFirst().value<QDBusObjectPath>().path();
        const QString transfer = m_sendTransfer;
        QTimer::singleShot(30000, this, [this, transfer] {
            if (m_sendTransfer == transfer) {
                qCWarning(lcMsg) << "sending timed out";
                endSend(false);
            }
        });
    });
}

void Messages::endSend(bool ok)
{
    if (m_sendTo.isEmpty())
        return;
    const QString number = m_sendTo;
    m_sendTo.clear();
    m_sendTransfer.clear();
    QFile::remove(m_sendFile);
    if (ok)
        emit sent(number);
    else
        emit sendFailed(number);
}

void Messages::onTransferChanged(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2 || args.at(0).toString() != TransferIface)
        return;
    const bool sending = !m_sendTransfer.isEmpty() && msg.path() == m_sendTransfer;
    if (!sending && (m_transfer.isEmpty() || msg.path() != m_transfer))
        return;
    QVariantMap changed;
    args.at(1).value<QDBusArgument>() >> changed;
    const QString status = changed.value(QStringLiteral("Status")).toString();
    if (sending) {
        if (status == QLatin1String("complete")) {
            qCInfo(lcMsg) << "reply sent";
            endSend(true);
        } else if (status == QLatin1String("error")) {
            qCWarning(lcMsg) << "sending the reply failed";
            endSend(false);
        }
        return;
    }
    if (status == QLatin1String("complete")) {
        QFile f(m_file);
        if (f.open(QIODevice::ReadOnly)) {
            fetched(f.readAll());
            return;
        }
        endFetch(false);
    } else if (status == QLatin1String("error")) {
        qCWarning(lcMsg) << "reading the message failed";
        endFetch(false);
    }
}

void Messages::fetched(const QByteArray &bmsg)
{
    const Bmsg m = parseBmsg(bmsg);
    const QString id = m_fetchId;
    endFetch(true);
    if (m.body.isEmpty()) {
        qCWarning(lcMsg) << "message" << id << "has no text";
        emit textFailed(id);
        return;
    }
    markRead(id);
    emit textReady(id, m.name, m.number, m.body);
}

// Shows as read on the phone too
void Messages::markRead(const QString &id)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, id, PropsIface, QStringLiteral("Set"));
    call << MessageIface << QStringLiteral("Read") << QVariant::fromValue(QDBusVariant(true));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [id](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError())
            qCWarning(lcMsg) << "cannot mark" << id << "read:" << w->error().message();
    });
}

void Messages::endFetch(bool ok)
{
    if (m_fetchId.isEmpty())
        return;
    const QString id = m_fetchId;
    m_fetchId.clear();
    m_transfer.clear();
    QFile::remove(m_file);
    if (!ok)
        emit textFailed(id);
}
