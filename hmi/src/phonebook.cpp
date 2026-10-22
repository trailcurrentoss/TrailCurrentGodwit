#include "phonebook.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QSet>
#include <QTimeZone>
#include <QTimer>

Q_LOGGING_CATEGORY(lcPb, "godwit.phonebook")

namespace {

const QString Service = QStringLiteral("org.bluez.obex");
const QString ClientIface = QStringLiteral("org.bluez.obex.Client1");
const QString PbapIface = QStringLiteral("org.bluez.obex.PhonebookAccess1");
const QString TransferIface = QStringLiteral("org.bluez.obex.Transfer1");
const QString PropsIface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString BusName = QStringLiteral("godwit-obex");
constexpr int MaxRecents = 30;

// A private connection, retried until the system bus is up (see connman.cpp)
QDBusConnection openBus()
{
    QDBusConnection::disconnectFromBus(BusName);
    return QDBusConnection::connectToBus(
        qEnvironmentVariable("GODWIT_OBEX_BUS") == QLatin1String("session")
            ? QDBusConnection::SessionBus : QDBusConnection::SystemBus,
        BusName);
}

// ------------------------------------------------------------------ vCard

struct Prop
{
    QString name;      // upper case
    QString params;    // upper case, as written: "TYPE=CELL;TYPE=VOICE"
    QString value;
};
using Card = QList<Prop>;

QString unescape(QString v)
{
    v.replace(QLatin1String("\\n"), QLatin1String(" ")).replace(QLatin1String("\\N"), QLatin1String(" "));
    v.replace(QLatin1String("\\,"), QLatin1String(",")).replace(QLatin1String("\\;"), QLatin1String(";"));
    return v.replace(QLatin1String("\\\\"), QLatin1String("\\")).trimmed();
}

QList<Card> parseCards(const QByteArray &data)
{
    QString text = QString::fromUtf8(data);
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    // Unfold continuation lines
    text.replace(QLatin1String("\n "), QString()).replace(QLatin1String("\n\t"), QString());
    QList<Card> cards;
    Card card;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.compare(QLatin1String("BEGIN:VCARD"), Qt::CaseInsensitive) == 0) {
            card.clear();
            continue;
        }
        if (line.compare(QLatin1String("END:VCARD"), Qt::CaseInsensitive) == 0) {
            cards.append(card);
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon);
        const int semi = key.indexOf(QLatin1Char(';'));
        card.append({ (semi < 0 ? key : key.left(semi)).toUpper(),
                      semi < 0 ? QString() : key.mid(semi + 1).toUpper(),
                      unescape(line.mid(colon + 1)) });
    }
    return cards;
}

QString field(const Card &card, const QString &name)
{
    for (const Prop &p : card)
        if (p.name == name && !p.value.isEmpty())
            return p.value;
    return {};
}

// FN, or "Given Family" from N when FN is empty
QString nameOf(const Card &card)
{
    const QString fn = field(card, QStringLiteral("FN"));
    if (!fn.isEmpty())
        return fn;
    const QStringList n = field(card, QStringLiteral("N")).split(QLatin1Char(';'));
    return QStringList { n.value(1), n.value(0) }.join(QLatin1Char(' ')).simplified();
}

// The mobile number if there is one, else the first
QString numberOf(const Card &card)
{
    QString first;
    for (const Prop &p : card) {
        if (p.name != QLatin1String("TEL") || p.value.isEmpty())
            continue;
        if (p.params.contains(QLatin1String("CELL")))
            return p.value;
        if (first.isEmpty())
            first = p.value;
    }
    return first;
}

QString initials(const QString &name)
{
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty() || !words.first().at(0).isLetter())
        return QStringLiteral("#");
    QString out = words.first().left(1);
    if (words.size() > 1 && words.last().at(0).isLetter())
        out += words.last().left(1);
    return out.toUpper();
}

// The digits that identify a number: the last ten, ignoring formatting and
// the country code
QString key(const QString &number)
{
    QString digits;
    for (const QChar c : number)
        if (c.isDigit())
            digits += c;
    return digits.right(10);
}

} // namespace

Phonebook::Phonebook(QObject *parent)
    : QObject(parent)
    , m_bus(openBus())
{
    // Only the head unit reads real phones. A PC running the simulator keeps
    // the demo contacts unless GODWIT_OBEX_BUS says which bus to use.
    if (qEnvironmentVariableIsEmpty("GODWIT_OBEX_BUS") && QGuiApplication::platformName() != QLatin1String("eglfs"))
        return;
    start();
}

void Phonebook::start()
{
    if (!m_bus.isConnected()) {
        if (++m_busTries < 120)
            QTimer::singleShot(500, this, [this] { m_bus = openBus(); start(); });
        return;
    }
    qCInfo(lcPb) << "connected to the bus";
    m_available = true;
    emit availableChanged();
    m_bus.connect(Service, QString(), PropsIface, QStringLiteral("PropertiesChanged"),
                  this, SLOT(onTransferChanged(QDBusMessage)));
    // obexd starting after a phone connected, or restarting after it
    // stopped mid-read: read the phone then
    auto *watcher = new QDBusServiceWatcher(Service, m_bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        if (!m_address.isEmpty() && !syncing())
            sync(m_address);
    });
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        if (!syncing())
            return;
        m_session.clear(); // gone with obexd
        finish(QStringLiteral("obexd stopped"));
    });
}

void Phonebook::sync(const QString &address, bool recentsOnly)
{
    if (!m_available)
        return;
    if (address.isEmpty()) {
        finish();
        m_address.clear();
        m_names.clear();
        m_contacts.clear();
        m_recents.clear();
        emit contactsChanged();
        emit recentsChanged();
        return;
    }
    if (syncing()) {
        if (address == m_address)
            return;
        finish();
    }
    // The same phone again: its contacts are already here, so only the call
    // history can be out of date
    if (address == m_address && !m_contacts.isEmpty())
        recentsOnly = true;
    if (address != m_address && !recentsOnly) {
        m_contacts.clear();
        m_recents.clear();
        emit contactsChanged();
        emit recentsChanged();
    }
    if (address != m_address)
        m_retries = 0;
    m_address = address;
    m_failed = false;
    m_steps = recentsOnly ? QStringList { QStringLiteral("cch") } : QStringList { QStringLiteral("pb"), QStringLiteral("cch") };
    emit syncingChanged();
    qCInfo(lcPb) << "reading" << m_steps << "from" << address;

    QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez/obex"), ClientIface, QStringLiteral("CreateSession"));
    call << address << QVariantMap { { QStringLiteral("Target"), QStringLiteral("PBAP") } };
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, address](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (address != m_address)
            return;
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            // The phone may not have allowed access yet; finish asks again
            finish(reply.errorName() + QLatin1Char(' ') + reply.errorMessage());
            return;
        }
        m_session = reply.arguments().constFirst().value<QDBusObjectPath>().path();
        next();
    });
}

void Phonebook::next()
{
    if (m_steps.isEmpty()) {
        finish();
        return;
    }
    m_book = m_steps.takeFirst();
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_session, PbapIface, QStringLiteral("Select"));
    call << QStringLiteral("int") << m_book;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcPb) << "cannot open" << m_book << ":" << w->error().message();
            m_failed = true;
            next();
            return;
        }
        pull(m_book);
    });
}

void Phonebook::pull(const QString &book)
{
    const QString dir = qEnvironmentVariable("XDG_CACHE_HOME", QDir::tempPath());
    m_file = dir + QStringLiteral("/godwit-") + book + QStringLiteral(".vcf");
    QFile::remove(m_file);
    QStringList fields { QStringLiteral("FN"), QStringLiteral("N"), QStringLiteral("TEL") };
    if (book == QLatin1String("cch"))
        fields << QStringLiteral("X-IRMC-CALL-DATETIME");
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_session, PbapIface, QStringLiteral("PullAll"));
    call << m_file << QVariantMap { { QStringLiteral("Format"), QStringLiteral("vcard30") },
                                    { QStringLiteral("Fields"), fields } };
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcPb) << "cannot read" << m_book << ":" << reply.errorMessage();
            m_failed = true;
            next();
            return;
        }
        m_transfer = reply.arguments().constFirst().value<QDBusObjectPath>().path();
        // A large phonebook takes a while; give up on one that stalls
        const QString transfer = m_transfer;
        QTimer::singleShot(120000, this, [this, transfer] {
            if (m_transfer == transfer) {
                m_transfer.clear();
                finish(QStringLiteral("timed out"));
            }
        });
    });
}

void Phonebook::onTransferChanged(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (m_transfer.isEmpty() || msg.path() != m_transfer || args.size() < 2 || args.at(0).toString() != TransferIface)
        return;
    QVariantMap changed;
    args.at(1).value<QDBusArgument>() >> changed;
    const QString status = changed.value(QStringLiteral("Status")).toString();
    if (status == QLatin1String("complete")) {
        m_transfer.clear();
        QFile f(m_file);
        if (f.open(QIODevice::ReadOnly))
            loaded(m_book, f.readAll());
        f.remove();
        next();
    } else if (status == QLatin1String("error")) {
        m_transfer.clear();
        qCWarning(lcPb) << "reading" << m_book << "failed";
        m_failed = true;
        next();
    }
}

void Phonebook::loaded(const QString &book, const QByteArray &vcards)
{
    const QList<Card> cards = parseCards(vcards);
    if (book == QLatin1String("pb")) {
        QVariantList list;
        QSet<QString> seen;
        m_names.clear();
        // The first card is the phone owner's own
        for (int i = 1; i < cards.size(); ++i) {
            const QString num = numberOf(cards.at(i));
            if (num.isEmpty())
                continue;
            QString name = nameOf(cards.at(i));
            if (name.isEmpty())
                name = num;
            for (const Prop &p : cards.at(i))
                if (p.name == QLatin1String("TEL") && key(p.value).size() >= 7)
                    m_names.insert(key(p.value), name);
            if (seen.contains(name + key(num)))
                continue;
            seen.insert(name + key(num));
            list.append(QVariantMap { { QStringLiteral("n"), name }, { QStringLiteral("num"), num },
                                      { QStringLiteral("ini"), initials(name) } });
        }
        std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("n")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("n")).toString()) < 0;
        });
        m_contacts = list;
        qCInfo(lcPb) << m_contacts.size() << "contacts";
        emit contactsChanged();
        return;
    }

    QVariantList list;
    for (const Card &card : cards) {
        QString type, stamp;
        for (const Prop &p : card) {
            if (p.name != QLatin1String("X-IRMC-CALL-DATETIME"))
                continue;
            type = p.params.contains(QLatin1String("MISSED")) ? QStringLiteral("missed")
                 : p.params.contains(QLatin1String("DIALED")) ? QStringLiteral("out") : QStringLiteral("in");
            stamp = p.value;
        }
        const QString num = numberOf(card);
        if (num.isEmpty() || type.isEmpty())
            continue;
        QString name = nameOf(card);
        if (name.isEmpty())
            name = nameFor(num);
        if (name.isEmpty())
            name = num;
        QDateTime when = QDateTime::fromString(stamp.left(15), QStringLiteral("yyyyMMdd'T'HHmmss"));
        if (stamp.endsWith(QLatin1Char('Z')))
            when = QDateTime(when.date(), when.time(), QTimeZone::UTC).toLocalTime();
        list.append(QVariantMap { { QStringLiteral("n"), name }, { QStringLiteral("num"), num },
                                  { QStringLiteral("ini"), initials(name) }, { QStringLiteral("type"), type },
                                  { QStringLiteral("when"), when } });
        if (list.size() == MaxRecents)
            break;
    }
    m_recents = list;
    qCInfo(lcPb) << m_recents.size() << "recent calls";
    emit recentsChanged();
}

void Phonebook::finish(const QString &error)
{
    if (!error.isEmpty()) {
        qCWarning(lcPb) << "phonebook not read:" << error;
        m_failed = true;
    }
    // A phone that did not answer, often because an earlier session was still
    // open on it, usually answers a little later
    if (m_failed && !m_address.isEmpty() && m_retries < 3) {
        ++m_retries;
        const QString address = m_address;
        QTimer::singleShot(30000, this, [this, address] {
            if (address == m_address && !syncing())
                sync(address);
        });
    } else if (!m_failed) {
        m_retries = 0;
    }
    m_failed = false;
    if (!m_session.isEmpty()) {
        QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez/obex"), ClientIface, QStringLiteral("RemoveSession"));
        call << QVariant::fromValue(QDBusObjectPath(m_session));
        m_bus.asyncCall(call);
    }
    const bool was = syncing();
    m_session.clear();
    m_steps.clear();
    m_transfer.clear();
    if (was)
        emit syncingChanged();
}

QString Phonebook::nameFor(const QString &number) const
{
    const QString k = key(number);
    return k.size() < 7 ? QString() : m_names.value(k);
}
