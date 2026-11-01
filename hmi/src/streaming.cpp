#include "streaming.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>

#include <algorithm>

Q_LOGGING_CATEGORY(lcStream, "godwit.music")

namespace {

QString str(const QVariantMap &m, const char *key, const QString &fallback = QString())
{
    const QString v = m.value(QLatin1String(key)).toString();
    return v.isEmpty() ? fallback : v;
}

// The receivers' default name: the head unit's host name
QString defaultName()
{
    const QString host = QHostInfo::localHostName();
    return host.isEmpty() || host == QLatin1String("localhost") ? QStringLiteral("Godwit") : host;
}

// Quoted for YAML and libconfig alike: both read "..." with \" and \\ escapes
QString quoted(QString s)
{
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    s.remove(QLatin1Char('\n'));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

bool writePrivate(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    f.write(data);
    return f.commit();
}

QVariantMap field(const QString &key, const QString &label, const QString &value, bool secret = false, const QString &hint = QString())
{
    return { { QStringLiteral("key"), key }, { QStringLiteral("label"), label }, { QStringLiteral("value"), value },
             { QStringLiteral("secret"), secret }, { QStringLiteral("hint"), hint } };
}

QVariantMap action(const QString &id, const QString &label)
{
    return { { QStringLiteral("id"), id }, { QStringLiteral("label"), label } };
}

} // namespace

// ---------------------------------------------------------------- settings

QVariantMap loadSettings(const QString &id)
{
    QFile f(Music::stateDir() + QLatin1Char('/') + id + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

void saveSettings(const QString &id, const QVariantMap &values)
{
    const QString path = Music::stateDir() + QLatin1Char('/') + id + QStringLiteral(".json");
    if (!writePrivate(path, QJsonDocument(QJsonObject::fromVariantMap(values)).toJson()))
        qCWarning(lcStream) << "could not save" << path;
}

// ---------------------------------------------------------------- Daemon

Daemon::Daemon(const QString &name, QObject *parent)
    : QObject(parent), m_name(name)
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &Daemon::launch);
}

Daemon::~Daemon()
{
    stop();
}

void Daemon::setProgram(const char *envVar, const QString &fallback)
{
    m_envVar = envVar;
    m_fallback = fallback;
    findProgram();
}

void Daemon::findProgram()
{
    const QString v = qEnvironmentVariable(m_envVar);
    m_program = !v.isEmpty() ? (QFileInfo(v).isExecutable() ? v : QString()) : QStandardPaths::findExecutable(m_fallback);
}

void Daemon::start()
{
    m_wanted = true;
    m_failures = 0;
    if (!running())
        launch();
}

void Daemon::stop()
{
    m_wanted = false;
    m_retry.stop();
    if (!m_proc)
        return;
    QProcess *p = m_proc;
    m_proc = nullptr;
    p->disconnect(this);
    p->terminate();
    if (!p->waitForFinished(2000))
        p->kill();
    p->waitForFinished(500);
    p->deleteLater();
}

void Daemon::restart()
{
    stop();
    start();
}

void Daemon::launch()
{
    if (!m_wanted || m_program.isEmpty() || running())
        return;
    auto *p = new QProcess(this);
    m_proc = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::readyRead, this, [this, p] {
        while (p->canReadLine()) {
            const QString l = QString::fromUtf8(p->readLine()).trimmed();
            if (!l.isEmpty())
                emit line(l);
        }
    });
    connect(p, &QProcess::started, this, &Daemon::started);
    connect(p, &QProcess::finished, this, [this, p](int code) {
        qCWarning(lcStream) << m_name << "exited with" << code;
        if (p == m_proc) {
            m_proc = nullptr;
            p->deleteLater();
        }
        emit exited(code);
        if (m_wanted) {
            // 2 s, 4 s ... up to a minute
            m_retry.start(std::min(60000, 2000 << std::min(m_failures, 5)));
            ++m_failures;
        }
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            qCWarning(lcStream) << m_name << "could not start:" << p->errorString();
            emit p->finished(-1, QProcess::CrashExit);
        }
    });
    qCInfo(lcStream) << "starting" << m_name << m_program << m_args;
    p->start(m_program, m_args);
}

// ---------------------------------------------------------------- Spotify

SpotifySource::SpotifySource(QObject *parent)
    : MusicSource(QStringLiteral("spotify"), QStringLiteral("Spotify"), parent)
{
    m_daemon = new Daemon(QStringLiteral("go-librespot"), this);
    m_daemon->setProgram("GODWIT_SPOTIFY_BIN", QStringLiteral("go-librespot"));
    m_daemon->setArguments({ QStringLiteral("--config_dir"), dir() });
    m_net = new QNetworkAccessManager(this);
    m_settings = loadSettings(id());

    connect(m_daemon, &Daemon::line, this, [](const QString &l) { qCDebug(lcStream).noquote() << "go-librespot:" << l; });
    connect(m_daemon, &Daemon::exited, this, [this] {
        m_misses = 0;
        setPlaying(false);
    });
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &SpotifySource::poll);

    enable(m_settings.value(QStringLiteral("enabled"), false).toBool());
}

QString SpotifySource::dir() const
{
    return Music::stateDir() + QStringLiteral("/spotify");
}

void SpotifySource::enable(bool on)
{
    m_settings.insert(QStringLiteral("enabled"), on);
    m_daemon->findProgram();
    if (!m_daemon->installed()) {
        setStatus(QStringLiteral("off"), QStringLiteral("Spotify is not installed on this head unit"));
        return;
    }
    if (!on) {
        m_daemon->stop();
        m_poll.stop();
        setPlaying(false);
        setStatus(QStringLiteral("setup"), QStringLiteral("Set up Spotify to sign in"));
        emit setupChanged();
        return;
    }
    writeConfig();
    m_daemon->start();
    m_poll.start();
    setStatus(QStringLiteral("starting"), QStringLiteral("Starting Spotify"));
    emit setupChanged();
}

// go-librespot's config.yml. The API listens on loopback only. Its own volume
// stays at full: the HMI's media volume is the volume.
void SpotifySource::writeConfig()
{
    QDir().mkpath(dir());
    QFile::setPermissions(dir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QString port = qEnvironmentVariable("GODWIT_SPOTIFY_PORT", QStringLiteral("3678"));
    const QString yml = QStringLiteral(
        "# Written by the Godwit HMI; changes here are overwritten\n"
        "device_name: %1\n"
        "device_type: automobile\n"
        "audio_backend: alsa\n"
        "audio_device: default\n"
        "zeroconf_enabled: false\n"
        "credentials:\n"
        "  type: device_auth\n"
        "server:\n"
        "  enabled: true\n"
        "  address: 127.0.0.1\n"
        "  port: %2\n"
        "metadata:\n"
        "  enabled: true\n"
        "  context_sweep: true\n"
        "initial_volume: 100\n"
        "ignore_last_volume: true\n")
        .arg(quoted(str(m_settings, "name", defaultName())), port);
    if (!writePrivate(dir() + QStringLiteral("/config.yml"), yml.toUtf8()))
        qCWarning(lcStream) << "could not write" << dir() + QStringLiteral("/config.yml");
}

QNetworkReply *SpotifySource::get(const QString &path)
{
    const QString port = qEnvironmentVariable("GODWIT_SPOTIFY_PORT", QStringLiteral("3678"));
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port, path)));
    req.setTransferTimeout(5000);
    return m_net->get(req);
}

void SpotifySource::post(const QString &path, const QJsonObject &body, const QString &failed)
{
    const QString port = qEnvironmentVariable("GODWIT_SPOTIFY_PORT", QStringLiteral("3678"));
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port, path)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(5000);
    QNetworkReply *r = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [this, r, path, failed] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) {
            qCWarning(lcStream) << "spotify" << path << r->errorString() << r->readAll();
            emit message(failed.isEmpty() ? QStringLiteral("Spotify did not respond") : failed);
        }
        poll();
    });
}

// The player's state, or while not signed in the pairing code
void SpotifySource::poll()
{
    if (!m_daemon->running())
        return;
    QNetworkReply *r = get(QStringLiteral("/status"));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() != QNetworkReply::NoError && code == 0) {
            // Still starting, or it has stopped answering
            if (++m_misses > 15)
                setStatus(QStringLiteral("error"), QStringLiteral("Spotify is not responding"));
            return;
        }
        m_misses = 0;
        if (code == 200) {
            status(QJsonDocument::fromJson(r->readAll()).object());
            return;
        }
        // No session: signing in, or waiting for the pairing code to be entered
        QNetworkReply *a = get(QStringLiteral("/auth/code"));
        connect(a, &QNetworkReply::finished, this, [this, a] {
            a->deleteLater();
            const int c = a->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QVariantMap pairing;
            if (c == 200) {
                const QJsonObject o = QJsonDocument::fromJson(a->readAll()).object();
                pairing = { { QStringLiteral("url"), o.value(QLatin1String("url")).toString() },
                            { QStringLiteral("code"), o.value(QLatin1String("code")).toString() } };
            }
            if (pairing != m_pairing) {
                m_pairing = pairing;
                emit setupChanged();
            }
            if (!m_pairing.isEmpty())
                setStatus(QStringLiteral("setup"), QStringLiteral("Sign in: enter %1 at spotify.com/pair").arg(str(m_pairing, "code")));
            else if (m_user.isEmpty())
                setStatus(QStringLiteral("starting"), QStringLiteral("Signing in to Spotify"));
        });
    });
}

void SpotifySource::status(const QJsonObject &s)
{
    const QString user = s.value(QLatin1String("username")).toString();
    if (user != m_user || !m_pairing.isEmpty()) {
        m_user = user;
        m_pairing.clear();
        emit setupChanged();
    }
    if (!m_listed) {
        m_listed = true;
        loadPlaylists();
    }

    const QJsonObject t = s.value(QLatin1String("track")).toObject();
    const bool stopped = s.value(QLatin1String("stopped")).toBool();
    const bool playing = !t.isEmpty() && !stopped && !s.value(QLatin1String("paused")).toBool();
    setStatus(QStringLiteral("ready"), t.isEmpty() ? QStringLiteral("Search or choose a playlist in Browse, or play to this head unit from the Spotify app")
                                                   : QStringLiteral("Signed in as ") + user);

    QStringList artists;
    for (const QJsonValue &a : t.value(QLatin1String("artist_names")).toArray())
        artists << a.toString();
    setTrack(t.isEmpty() ? QVariantMap() : QVariantMap{
        { QStringLiteral("title"), t.value(QLatin1String("name")).toString() },
        { QStringLiteral("artist"), artists.join(QStringLiteral(", ")) },
        { QStringLiteral("album"), t.value(QLatin1String("album_name")).toString() },
        { QStringLiteral("art"), t.value(QLatin1String("album_cover_url")).toString() },
        { QStringLiteral("dur"), t.value(QLatin1String("duration")).toInt() / 1000 } });

    const double pos = t.value(QLatin1String("position")).toDouble() / 1000.0;
    const double expected = position() + (this->playing() ? (QDateTime::currentMSecsSinceEpoch() - positionAt()) / 1000.0 : 0);
    if (playing != this->playing() || std::abs(expected - pos) > 1.5)
        setPosition(pos);
    setPlaying(playing);
    setShuffleFlag(s.value(QLatin1String("shuffle_context")).toBool());
    if (playing && !m_wasPlaying)
        emit startedElsewhere();
    m_wasPlaying = playing;

    const QString context = s.value(QLatin1String("context_uri")).toString();
    m_trackUri = t.value(QLatin1String("uri")).toString();
    if (context != m_context) {
        m_context = context;
        m_queueTitle = s.value(QLatin1String("context_name")).toString();
        m_queue.clear();
        m_queueUris.clear();
        m_contextTries = 0;
        if (!context.isEmpty())
            loadContext(context);
    }
    const int index = int(m_queueUris.indexOf(m_trackUri));
    if (index != m_queueIndex) {
        m_queueIndex = index;
        emit queueChanged();
    }
}

void SpotifySource::loadPlaylists()
{
    QNetworkReply *r = get(QStringLiteral("/library/playlists?limit=200"));
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        if (r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            m_listed = false;
            return;
        }
        QVariantList lists;
        const QJsonArray items = QJsonDocument::fromJson(r->readAll()).object().value(QLatin1String("items")).toArray();
        for (const QJsonValue &v : items) {
            const QJsonObject p = v.toObject();
            const int n = p.value(QLatin1String("length")).toInt();
            if (n == 0)
                continue;  // nothing to play
            lists.append(QVariantMap{ { QStringLiteral("id"), p.value(QLatin1String("uri")).toString() },
                                      { QStringLiteral("title"), p.value(QLatin1String("name")).toString() },
                                      { QStringLiteral("sub"), QStringLiteral("Your playlist · %1 songs").arg(n) },
                                      { QStringLiteral("icon"), QStringLiteral("list") } });
        }
        // Last, as it may be empty; playing an empty one says so
        lists.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("spotify:user:%1:collection").arg(m_user) },
                                  { QStringLiteral("title"), QStringLiteral("Liked Songs") },
                                  { QStringLiteral("sub"), QStringLiteral("Your library") },
                                  { QStringLiteral("icon"), QStringLiteral("heart") } });
        m_playlists = lists;
        showLists();
    });
}

void SpotifySource::showLists()
{
    setLists(m_results + m_playlists);
}

// A token for Spotify's Web API: the driver's own Spotify app's, by the
// client credentials flow, when its ID and secret are set; otherwise the one
// go-librespot holds for the session (POST /token). Spotify rate-limits the
// latter heavily, since every librespot-based player shares its client ID.
// done(token, error) with error "" or "app" (the app's ID or secret refused).
void SpotifySource::accessToken(const std::function<void(const QString &, const QString &)> &done)
{
    const QString id = str(m_settings, "apiClientId"), secret = str(m_settings, "apiClientSecret");
    if (!id.isEmpty() && !secret.isEmpty()) {
        if (!m_appToken.isEmpty() && QDateTime::currentMSecsSinceEpoch() < m_appTokenUntil) {
            done(m_appToken, QString());
            return;
        }
        QNetworkRequest req(QUrl(qEnvironmentVariable("GODWIT_SPOTIFY_ACCOUNTS", QStringLiteral("https://accounts.spotify.com")) + QStringLiteral("/api/token")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        req.setRawHeader("Authorization", "Basic " + (id + QLatin1Char(':') + secret).toUtf8().toBase64());
        req.setTransferTimeout(10000);
        QNetworkReply *r = m_net->post(req, QByteArrayLiteral("grant_type=client_credentials"));
        connect(r, &QNetworkReply::finished, this, [this, r, done] {
            r->deleteLater();
            const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            m_appToken = o.value(QLatin1String("access_token")).toString();
            if (m_appToken.isEmpty()) {
                qCWarning(lcStream) << "spotify app token:" << code << r->errorString() << o.value(QLatin1String("error")).toString();
                done(QString(), code == 400 || code == 401 ? QStringLiteral("app") : QStringLiteral("failed"));
                return;
            }
            // A minute early, so a search never starts with a token about to lapse
            m_appTokenUntil = QDateTime::currentMSecsSinceEpoch() + (o.value(QLatin1String("expires_in")).toInt(3600) - 60) * 1000LL;
            done(m_appToken, QString());
        });
        return;
    }
    const QString port = qEnvironmentVariable("GODWIT_SPOTIFY_PORT", QStringLiteral("3678"));
    QNetworkRequest treq(QUrl(QStringLiteral("http://127.0.0.1:%1/token").arg(port)));
    treq.setTransferTimeout(10000);
    QNetworkReply *t = m_net->post(treq, QByteArray());
    connect(t, &QNetworkReply::finished, this, [t, done] {
        t->deleteLater();
        const QString token = QJsonDocument::fromJson(t->readAll()).object().value(QLatin1String("token")).toString();
        if (token.isEmpty())
            qCWarning(lcStream) << "spotify: no access token" << t->errorString();
        done(token, token.isEmpty() ? QStringLiteral("failed") : QString());
    });
}

// Spotify's catalogue search. Results are rows for Browse: a song starts a
// radio from it, an artist plays their music or a radio, an album or
// playlist plays from its start. done(rows, error): error is "" (rows may
// be empty: nothing matched), "busy" (Spotify said to wait; until then no
// search is sent), "app" or "failed".
void SpotifySource::webSearch(const QString &text, const std::function<void(const QVariantList &, const QString &)> &done)
{
    if (QDateTime::currentMSecsSinceEpoch() < m_searchBlockedUntil) {
        done({}, QStringLiteral("busy"));
        return;
    }
    accessToken([this, text, done](const QString &token, const QString &error) {
        if (token.isEmpty()) {
            done({}, error);
            return;
        }
        QUrl url(qEnvironmentVariable("GODWIT_SPOTIFY_WEB_API", QStringLiteral("https://api.spotify.com")) + QStringLiteral("/v1/search"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), text);
        q.addQueryItem(QStringLiteral("type"), QStringLiteral("track,artist,album,playlist"));
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
        url.setQuery(q);
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        req.setTransferTimeout(10000);
        QNetworkReply *r = m_net->get(req);
        connect(r, &QNetworkReply::finished, this, [this, r, done] {
            r->deleteLater();
            const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (code == 429) {
                // Retry-After in seconds; a minute when it is missing
                const int wait = std::clamp(r->rawHeader("Retry-After").toInt(), 0, 3600);
                m_retryAfter = wait > 0 ? wait : 60;
                m_searchBlockedUntil = QDateTime::currentMSecsSinceEpoch() + m_retryAfter * 1000LL;
                qCWarning(lcStream) << "spotify search: rate limited for" << m_retryAfter << "s";
                done({}, QStringLiteral("busy"));
                return;
            }
            if (code == 401)
                m_appToken.clear();
            if (r->error() != QNetworkReply::NoError) {
                qCWarning(lcStream) << "spotify search:" << r->errorString();
                done({}, QStringLiteral("failed"));
                return;
            }
            const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
            auto items = [&o](const char *kind) { return o.value(QLatin1String(kind)).toObject().value(QLatin1String("items")).toArray(); };
            auto names = [](const QJsonArray &a) {
                QStringList n;
                for (const QJsonValue &v : a)
                    n << v.toObject().value(QLatin1String("name")).toString();
                return n.join(QStringLiteral(", "));
            };
            auto idOf = [](const QJsonObject &x) { return x.value(QLatin1String("uri")).toString().section(QLatin1Char(':'), 2); };
            QVariantList out;
            // The top artist first, with a radio of music like theirs
            const QJsonArray artists = items("artists");
            if (!artists.isEmpty()) {
                const QJsonObject a = artists.first().toObject();
                const QString name = a.value(QLatin1String("name")).toString();
                out << browseRow(a.value(QLatin1String("uri")).toString(), name, QStringLiteral("Artist"), QStringLiteral("person"))
                    << browseRow(QStringLiteral("spotify:station:artist:") + idOf(a), name + QStringLiteral(" Radio"),
                                 QStringLiteral("Radio · music like ") + name, QStringLiteral("radio-outline"));
            }
            int n = 0;
            for (const QJsonValue &v : items("tracks")) {
                const QJsonObject tr = v.toObject();
                if (tr.isEmpty() || ++n > 6)
                    continue;
                out << browseRow(QStringLiteral("spotify:station:track:") + idOf(tr), tr.value(QLatin1String("name")).toString(),
                                 QStringLiteral("Song · ") + names(tr.value(QLatin1String("artists")).toArray()) + QStringLiteral(" · plays with a radio"),
                                 QStringLiteral("musical-note"));
            }
            n = 0;
            for (const QJsonValue &v : items("albums")) {
                const QJsonObject al = v.toObject();
                if (al.isEmpty() || ++n > 3)
                    continue;
                out << browseRow(al.value(QLatin1String("uri")).toString(), al.value(QLatin1String("name")).toString(),
                                 QStringLiteral("Album · ") + names(al.value(QLatin1String("artists")).toArray()), QStringLiteral("disc"));
            }
            n = 0;
            // Playlists the API no longer shows come back as nulls
            for (const QJsonValue &v : items("playlists")) {
                const QJsonObject pl = v.toObject();
                if (pl.isEmpty() || ++n > 3)
                    continue;
                out << browseRow(pl.value(QLatin1String("uri")).toString(), pl.value(QLatin1String("name")).toString(),
                                 QStringLiteral("Playlist · ") + pl.value(QLatin1String("owner")).toObject().value(QLatin1String("display_name")).toString(),
                                 QStringLiteral("list"));
            }
            done(out, QString());
        });
    });
}

QVariantMap SpotifySource::browseRow(const QString &id, const QString &title, const QString &sub, const QString &icon)
{
    return { { QStringLiteral("id"), id }, { QStringLiteral("title"), title }, { QStringLiteral("sub"), sub }, { QStringLiteral("icon"), icon } };
}

// What to tell the driver when a search could not run, as opposed to
// finding nothing
QString SpotifySource::searchProblem(const QString &error) const
{
    const bool ownApp = !str(m_settings, "apiClientId").isEmpty() && !str(m_settings, "apiClientSecret").isEmpty();
    if (error == QLatin1String("busy")) {
        const qint64 left = std::max<qint64>(1, (m_searchBlockedUntil - QDateTime::currentMSecsSinceEpoch() + 999) / 1000);
        return ownApp ? QStringLiteral("Spotify is busy. Try again in %1 s").arg(left)
                      : QStringLiteral("Spotify is limiting searches. Add your own Spotify app in Spotify settings");
    }
    if (error == QLatin1String("app"))
        return QStringLiteral("Spotify did not accept the app's client ID or secret");
    return QStringLiteral("Spotify search is not available right now");
}

void SpotifySource::search(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    if (m_user.isEmpty()) {
        emit message(QStringLiteral("Sign in to Spotify first"));
        return;
    }
    setBusy(true);
    webSearch(text.trimmed(), [this](const QVariantList &rows, const QString &error) {
        setBusy(false);
        if (!error.isEmpty()) {
            emit message(searchProblem(error));
            return;
        }
        if (rows.isEmpty()) {
            emit message(QStringLiteral("Nothing found on Spotify"));
            return;
        }
        m_results = rows;
        showLists();
    });
}

// The first row: the top artist when the words name one, else the top song's
// radio. When search cannot run, the account's own music stands in.
void SpotifySource::playSearch(const QString &text)
{
    if (m_user.isEmpty()) {
        emit message(QStringLiteral("Sign in to Spotify first"));
        return;
    }
    setBusy(true);
    webSearch(text.trimmed(), [this, text](const QVariantList &rows, const QString &error) {
        if (!error.isEmpty()) {
            playFromLibrary(text, searchProblem(error), 0);
            return;
        }
        setBusy(false);
        if (rows.isEmpty()) {
            emit message(QStringLiteral("Nothing found on Spotify for ") + text);
            return;
        }
        m_results = rows;
        showLists();
        // An artist named exactly plays their music; otherwise the song
        QVariantMap pick = rows.constFirst().toMap();
        if (pick.value(QStringLiteral("sub")).toString() == QLatin1String("Artist")
            && pick.value(QStringLiteral("title")).toString().compare(text.trimmed(), Qt::CaseInsensitive) != 0) {
            for (const QVariant &v : rows)
                if (v.toMap().value(QStringLiteral("id")).toString().startsWith(QLatin1String("spotify:station:track:"))) {
                    pick = v.toMap();
                    break;
                }
        }
        emit message(QStringLiteral("Playing ") + pick.value(QStringLiteral("title")).toString());
        openList(pick.value(QStringLiteral("id")).toString());
    });
}

// Without the Web API: a playlist of the account's by name, else an artist
// or song in Liked Songs (whose listing go-librespot fetches through
// Spotify's own services, not the Web API), started as a radio. tries counts
// the waits for the listing, which go-librespot builds in the background.
void SpotifySource::playFromLibrary(const QString &text, const QString &problem, int tries)
{
    const QString q = text.trimmed();
    for (const QVariant &v : std::as_const(m_playlists)) {
        const QVariantMap p = v.toMap();
        if (!p.value(QStringLiteral("id")).toString().endsWith(QLatin1String(":collection"))
            && p.value(QStringLiteral("title")).toString().contains(q, Qt::CaseInsensitive)) {
            setBusy(false);
            emit message(problem + QStringLiteral(" · playing your playlist ") + p.value(QStringLiteral("title")).toString());
            openList(p.value(QStringLiteral("id")).toString());
            return;
        }
    }
    const QString liked = QStringLiteral("spotify:user:%1:collection").arg(m_user);
    QNetworkReply *r = get(QStringLiteral("/context/tracks?uri=") + QString::fromLatin1(QUrl::toPercentEncoding(liked)));
    connect(r, &QNetworkReply::finished, this, [this, r, text, q, problem, tries] {
        r->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        const QJsonArray tracks = o.value(QLatin1String("tracks")).toArray();
        QString artistUri, artistName, trackUri, trackName;
        for (const QJsonValue &v : tracks) {
            const QJsonObject e = v.toObject(), t = e.value(QLatin1String("track")).toObject();
            const QJsonArray names = t.value(QLatin1String("artist_names")).toArray(), uris = t.value(QLatin1String("artist_uris")).toArray();
            for (int i = 0; i < names.size() && artistUri.isEmpty(); ++i)
                if (names[i].toString().compare(q, Qt::CaseInsensitive) == 0 && i < uris.size()) {
                    artistUri = uris[i].toString();
                    artistName = names[i].toString();
                }
            if (trackUri.isEmpty() && t.value(QLatin1String("name")).toString().contains(q, Qt::CaseInsensitive)) {
                trackUri = e.value(QLatin1String("uri")).toString();
                trackName = t.value(QLatin1String("name")).toString();
            }
        }
        if (!artistUri.isEmpty() || !trackUri.isEmpty()) {
            setBusy(false);
            const bool artist = !artistUri.isEmpty();
            emit message(problem + QStringLiteral(" · playing ") + (artist ? artistName + QStringLiteral(" Radio") : trackName));
            openList(QStringLiteral("spotify:station:") + (artist ? QStringLiteral("artist:") + artistUri.section(QLatin1Char(':'), 2)
                                                                  : QStringLiteral("track:") + trackUri.section(QLatin1Char(':'), 2)));
            return;
        }
        const bool more = !o.value(QLatin1String("ready")).toBool()
            || o.value(QLatin1String("cached")).toInt() < o.value(QLatin1String("length")).toInt();
        if (more && tries < 4) {
            QTimer::singleShot(1500, this, [this, text, problem, tries] { playFromLibrary(text, problem, tries + 1); });
            return;
        }
        setBusy(false);
        emit message(problem);
    });
}

// The tracks of what is playing, for Up Next. The daemon lists them in the
// background; ask again until it has them all.
void SpotifySource::loadContext(const QString &uri)
{
    ++m_contextTries;
    QNetworkReply *r = get(QStringLiteral("/context/tracks?uri=") + QString::fromLatin1(QUrl::toPercentEncoding(uri)));
    connect(r, &QNetworkReply::finished, this, [this, r, uri] {
        r->deleteLater();
        if (uri != m_context || r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        QVariantList queue;
        QStringList uris;
        for (const QJsonValue &v : o.value(QLatin1String("tracks")).toArray()) {
            const QJsonObject e = v.toObject();
            const QJsonObject t = e.value(QLatin1String("track")).toObject();
            QStringList artists;
            for (const QJsonValue &a : t.value(QLatin1String("artist_names")).toArray())
                artists << a.toString();
            uris << e.value(QLatin1String("uri")).toString();
            queue.append(QVariantMap{ { QStringLiteral("title"), t.isEmpty() ? QStringLiteral("…") : t.value(QLatin1String("name")).toString() },
                                      { QStringLiteral("artist"), artists.join(QStringLiteral(", ")) },
                                      { QStringLiteral("dur"), t.value(QLatin1String("duration")).toInt() / 1000 } });
        }
        m_queue = queue;
        m_queueUris = uris;
        m_queueIndex = int(m_queueUris.indexOf(m_trackUri));
        emit queueChanged();
        const bool done = o.value(QLatin1String("ready")).toBool()
            && o.value(QLatin1String("cached")).toInt() >= o.value(QLatin1String("length")).toInt();
        // A listing still empty after a while will not come: the context
        // has no tracks (Liked Songs on a new account), or the daemon could
        // not list it (a radio station). Up Next stays empty; playback goes on.
        const bool hopeless = !done && queue.isEmpty() && m_contextTries >= 8;
        if (hopeless)
            qCInfo(lcStream) << "spotify: no track listing for" << uri;
        if (!done && !hopeless && m_contextTries < 120)
            QTimer::singleShot(1500, this, [this, uri] { if (uri == m_context) loadContext(uri); });
    });
}

void SpotifySource::play()
{
    if (track().isEmpty() && !lists().isEmpty()) {
        openList(lists().constFirst().toMap().value(QStringLiteral("id")).toString());
        return;
    }
    post(QStringLiteral("/player/resume"));
}

void SpotifySource::pause() { post(QStringLiteral("/player/pause")); }
void SpotifySource::next() { post(QStringLiteral("/player/next")); }
void SpotifySource::previous() { post(QStringLiteral("/player/prev")); }

void SpotifySource::playAt(int index)
{
    if (index < 0 || index >= m_queueUris.size() || m_context.isEmpty())
        return;
    post(QStringLiteral("/player/play"), { { QStringLiteral("uri"), m_context }, { QStringLiteral("skip_to_uri"), m_queueUris[index] } });
}

void SpotifySource::setShuffle(bool on)
{
    setShuffleFlag(on);
    post(QStringLiteral("/player/shuffle_context"), { { QStringLiteral("shuffle_context"), on } });
}

void SpotifySource::openList(const QString &id)
{
    const QString failed = id.endsWith(QLatin1String(":collection")) ? QStringLiteral("No liked songs yet")
                                                                     : QStringLiteral("Spotify could not play that");
    post(QStringLiteral("/player/play"), { { QStringLiteral("uri"), id } }, failed);
}

QVariantMap SpotifySource::setupInfo() const
{
    const bool on = m_settings.value(QStringLiteral("enabled"), false).toBool();
    QVariantList actions;
    if (!m_user.isEmpty())
        actions << action(QStringLiteral("signout"), QStringLiteral("Sign Out"));
    actions << action(on ? QStringLiteral("disable") : QStringLiteral("enable"), on ? QStringLiteral("Turn Off") : QStringLiteral("Turn On"));
    return {
        { QStringLiteral("title"), QStringLiteral("Spotify") },
        { QStringLiteral("installed"), m_daemon->installed() },
        { QStringLiteral("enabled"), on },
        { QStringLiteral("signedIn"), !m_user.isEmpty() },
        { QStringLiteral("account"), m_user },
        { QStringLiteral("pairing"), m_pairing },
        { QStringLiteral("fields"), QVariantList{
              field(QStringLiteral("name"), QStringLiteral("Device Name"), str(m_settings, "name", defaultName()), false,
                    QStringLiteral("How the head unit appears in the Spotify app")),
              field(QStringLiteral("apiClientId"), QStringLiteral("Search App Client ID"), str(m_settings, "apiClientId"), false,
                    QStringLiteral("Optional. Your Spotify app, for reliable search")),
              field(QStringLiteral("apiClientSecret"), QStringLiteral("Search App Client Secret"), str(m_settings, "apiClientSecret"), true,
                    QStringLiteral("From the same app")) } },
        { QStringLiteral("actions"), actions },
        { QStringLiteral("steps"), QStringList{
              QStringLiteral("Needs Spotify Premium and an internet connection."),
              QStringLiteral("On a phone or computer, go to spotify.com/pair and enter the code shown here."),
              QStringLiteral("Once signed in, search or choose a playlist in Browse (typing needs Park; the knob and \"Play …\" by voice work while driving), or pick this head unit from the devices list in the Spotify app."),
              QStringLiteral("For reliable search, add your own Spotify app: at developer.spotify.com/dashboard, Create app (any name; Redirect URI http://127.0.0.1:8888/callback, which is not used; tick Web API), then enter its Client ID and Client Secret above. Without one, Spotify often limits searches.") } },
    };
}

void SpotifySource::applySettings(const QVariantMap &values)
{
    bool app = false;
    for (const QString &k : { QStringLiteral("apiClientId"), QStringLiteral("apiClientSecret") }) {
        if (!values.contains(k))
            continue;
        const QString v = values.value(k).toString().trimmed();
        if (v != str(m_settings, k.toLatin1().constData())) {
            m_settings.insert(k, v);
            app = true;
        }
    }
    if (app) {
        // A new app gets its own token, and its own rate limit
        m_appToken.clear();
        m_searchBlockedUntil = 0;
        saveSettings(id(), m_settings);
        emit setupChanged();
    }
    const QString name = values.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty() || name == str(m_settings, "name"))
        return;
    m_settings.insert(QStringLiteral("name"), name);
    saveSettings(id(), m_settings);
    writeConfig();
    post(QStringLiteral("/set_device_name"), { { QStringLiteral("name"), name } });
    emit setupChanged();
}

// Forgets the stored login (in state.json beside the config) and starts over
// with a new pairing code
void SpotifySource::signOut()
{
    m_daemon->stop();
    QFile::remove(dir() + QStringLiteral("/state.json"));
    m_user.clear();
    m_listed = false;
    m_playlists.clear();
    m_results.clear();
    m_context.clear();
    m_queue.clear();
    m_queueUris.clear();
    m_queueIndex = -1;
    setLists({});
    setTrack({});
    setPlaying(false);
    emit queueChanged();
    emit setupChanged();
    if (m_settings.value(QStringLiteral("enabled"), false).toBool()) {
        m_daemon->start();
        setStatus(QStringLiteral("starting"), QStringLiteral("Signing in to Spotify"));
    }
}

// ---------------------------------------------------------------- AirPlay

namespace {
const QString MprisService = QStringLiteral("org.mpris.MediaPlayer2.ShairportSync");
const QString MprisPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString MprisPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString DBusProps = QStringLiteral("org.freedesktop.DBus.Properties");
}

AirPlaySource::AirPlaySource(QObject *parent)
    : MusicSource(QStringLiteral("apple"), QStringLiteral("Apple Music"), parent),
      m_bus(qEnvironmentVariable("GODWIT_AIRPLAY_BUS") == QLatin1String("session") ? QDBusConnection::sessionBus() : QDBusConnection::systemBus())
{
    m_daemon = new Daemon(QStringLiteral("shairport-sync"), this);
    m_daemon->setProgram("GODWIT_AIRPLAY_BIN", QStringLiteral("shairport-sync"));
    m_conf = Music::stateDir() + QStringLiteral("/airplay.conf");
    m_daemon->setArguments({ QStringLiteral("--configfile=") + m_conf });
    m_settings = loadSettings(id());
    connect(m_daemon, &Daemon::line, this, [](const QString &l) { qCDebug(lcStream).noquote() << "shairport-sync:" << l; });
    connect(m_daemon, &Daemon::started, this, [this] {
        setStatus(QStringLiteral("ready"), QStringLiteral("On your iPhone, choose %1 under AirPlay").arg(str(m_settings, "name", defaultName())));
        QTimer::singleShot(1500, this, &AirPlaySource::refresh);
    });
    connect(m_daemon, &Daemon::exited, this, [this] {
        setPlaying(false);
        setTrack({});
    });

    m_bus.connect(MprisService, MprisPath, DBusProps, QStringLiteral("PropertiesChanged"),
                  this, SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
    enable(m_settings.value(QStringLiteral("enabled"), true).toBool());
}

void AirPlaySource::enable(bool on)
{
    m_settings.insert(QStringLiteral("enabled"), on);
    m_daemon->findProgram();
    if (!m_daemon->installed()) {
        setStatus(QStringLiteral("off"), QStringLiteral("AirPlay is not installed on this head unit"));
        return;
    }
    if (!m_bus.isConnected()) {
        setStatus(QStringLiteral("off"), QStringLiteral("AirPlay needs the system bus"));
        return;
    }
    if (!on) {
        m_daemon->stop();
        setPlaying(false);
        setTrack({});
        setStatus(QStringLiteral("setup"), QStringLiteral("AirPlay is turned off"));
        emit setupChanged();
        return;
    }
    writeConfig();
    m_daemon->start();
    setStatus(QStringLiteral("starting"), QStringLiteral("Starting AirPlay"));
    emit setupChanged();
}

// shairport-sync's settings (libconfig). Classic AirPlay, which takes a
// password and passes play, pause and skip back to the phone. Sound goes to
// ALSA's default device, PipeWire, where it takes the multimedia role.
void AirPlaySource::writeConfig()
{
    const QString art = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/godwit-airplay");
    QDir().mkpath(art);
    QString conf = QStringLiteral(
        "// Written by the Godwit HMI; changes here are overwritten\n"
        "general = {\n"
        "  name = %1;\n"
        "%2"
        "  output_backend = \"alsa\";\n"
        "  mdns_backend = \"avahi\";\n"
        "  ignore_volume_control = \"yes\";\n"
        "  dbus_service_bus = \"system\";\n"
        "  mpris_service_bus = \"system\";\n"
        "};\n"
        "alsa = { output_device = \"default\"; };\n"
        "metadata = { enabled = \"yes\"; include_cover_art = \"yes\"; cover_art_cache_directory = %3; };\n")
        .arg(quoted(str(m_settings, "name", defaultName())),
             str(m_settings, "password").isEmpty() ? QString() : QStringLiteral("  password = %1;\n").arg(quoted(str(m_settings, "password"))),
             quoted(art));
    if (!writePrivate(m_conf, conf.toUtf8()))
        qCWarning(lcStream) << "could not write" << m_conf;
}

void AirPlaySource::call(const QString &method)
{
    QDBusMessage m = QDBusMessage::createMethodCall(MprisService, MprisPath, MprisPlayer, method);
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, method] {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcStream) << "airplay" << method << w->error().message();
            emit message(QStringLiteral("Control playback on the iPhone"));
        }
    });
}

void AirPlaySource::refresh()
{
    QDBusMessage m = QDBusMessage::createMethodCall(MprisService, MprisPath, DBusProps, QStringLiteral("GetAll"));
    m << MprisPlayer;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, 3000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> r = *w;
        if (!r.isError())
            apply(r.value());
    });
}

void AirPlaySource::propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &invalidated)
{
    Q_UNUSED(invalidated)
    if (iface == MprisPlayer)
        apply(changed);
}

void AirPlaySource::apply(const QVariantMap &props)
{
    if (props.contains(QStringLiteral("Metadata"))) {
        QVariantMap md;
        qvariant_cast<QDBusArgument>(props.value(QStringLiteral("Metadata"))) >> md;
        if (md.isEmpty())
            md = props.value(QStringLiteral("Metadata")).toMap();
        const QString title = md.value(QStringLiteral("xesam:title")).toString();
        setTrack(title.isEmpty() ? QVariantMap() : QVariantMap{
            { QStringLiteral("title"), title },
            { QStringLiteral("artist"), md.value(QStringLiteral("xesam:artist")).toStringList().join(QStringLiteral(", ")) },
            { QStringLiteral("album"), md.value(QStringLiteral("xesam:album")).toString() },
            { QStringLiteral("art"), md.value(QStringLiteral("mpris:artUrl")).toString() },
            { QStringLiteral("dur"), int(md.value(QStringLiteral("mpris:length")).toLongLong() / 1000000) } });
    }
    if (props.contains(QStringLiteral("Position")))
        setPosition(props.value(QStringLiteral("Position")).toLongLong() / 1e6);
    if (props.contains(QStringLiteral("PlaybackStatus"))) {
        const bool playing = props.value(QStringLiteral("PlaybackStatus")).toString() == QLatin1String("Playing");
        if (playing != this->playing())
            setPosition(position());
        setPlaying(playing);
        if (playing && !m_wasPlaying)
            emit startedElsewhere();
        m_wasPlaying = playing;
    }
}

QVariantMap AirPlaySource::setupInfo() const
{
    const bool on = m_settings.value(QStringLiteral("enabled"), true).toBool();
    return {
        { QStringLiteral("title"), QStringLiteral("Apple Music") },
        { QStringLiteral("installed"), m_daemon->installed() },
        { QStringLiteral("enabled"), on },
        { QStringLiteral("signedIn"), false },
        { QStringLiteral("fields"), QVariantList{
              field(QStringLiteral("name"), QStringLiteral("AirPlay Name"), str(m_settings, "name", defaultName()), false,
                    QStringLiteral("How the head unit appears in the iPhone's AirPlay list")),
              field(QStringLiteral("password"), QStringLiteral("AirPlay Password"), str(m_settings, "password"), true,
                    QStringLiteral("Optional. Asked for by the iPhone the first time it connects.")) } },
        { QStringLiteral("actions"), QVariantList{ action(on ? QStringLiteral("disable") : QStringLiteral("enable"), on ? QStringLiteral("Turn Off") : QStringLiteral("Turn On")) } },
        { QStringLiteral("steps"), QStringList{
              QStringLiteral("Apple Music plays from your iPhone over AirPlay. Apple does not offer a way to sign in to Apple Music on other car systems."),
              QStringLiteral("Join the iPhone and the head unit to the same Wi-Fi network (the iPhone's Personal Hotspot works)."),
              QStringLiteral("In Apple Music, tap the AirPlay button and choose this head unit.") } },
    };
}

void AirPlaySource::applySettings(const QVariantMap &values)
{
    bool changed = false;
    for (const QString &k : { QStringLiteral("name"), QStringLiteral("password") }) {
        if (!values.contains(k))
            continue;
        const QString v = values.value(k).toString().trimmed();
        if (k == QLatin1String("name") && v.isEmpty())
            continue;
        if (v != str(m_settings, k.toLatin1().constData())) {
            m_settings.insert(k, v);
            changed = true;
        }
    }
    if (!changed)
        return;
    saveSettings(id(), m_settings);
    if (m_settings.value(QStringLiteral("enabled"), true).toBool()) {
        writeConfig();
        m_daemon->restart();
    }
    emit setupChanged();
}

// ---------------------------------------------------------------- YouTube

YouTubeSource::YouTubeSource(QObject *parent)
    : QueueSource(QStringLiteral("youtube"), QStringLiteral("YouTube Music"), parent)
{
    m_daemon = new Daemon(QStringLiteral("godwit-ytmusic"), this);
    m_daemon->setProgram("GODWIT_YTMUSIC_BIN", QStringLiteral("godwit-ytmusic"));
    const QString dir = Music::stateDir() + QStringLiteral("/youtube");
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    m_daemon->setArguments({ QStringLiteral("--state"), dir });
    m_settings = loadSettings(id());

    connect(m_daemon, &Daemon::line, this, &YouTubeSource::received);
    connect(m_daemon, &Daemon::started, this, [this] {
        request({ { QStringLiteral("cmd"), QStringLiteral("status") } }, [this](const QJsonObject &r) {
            m_signedIn = r.value(QLatin1String("signedIn")).toBool();
            m_account = r.value(QLatin1String("account")).toString();
            setStatus(QStringLiteral("ready"), m_signedIn ? QStringLiteral("Signed in") : QStringLiteral("Search for music to play"));
            emit setupChanged();
            loadLists();
        });
    });
    connect(m_daemon, &Daemon::exited, this, [this] {
        // Anything waiting on the helper will not hear back
        const auto waiting = m_waiting;
        m_waiting.clear();
        for (const Reply &r : waiting)
            if (r)
                r({ { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("stopped") } });
    });
    enable(m_settings.value(QStringLiteral("enabled"), true).toBool());
}

void YouTubeSource::enable(bool on)
{
    m_settings.insert(QStringLiteral("enabled"), on);
    m_daemon->findProgram();
    if (!m_daemon->installed()) {
        setStatus(QStringLiteral("off"), QStringLiteral("YouTube Music is not installed on this head unit"));
        return;
    }
    if (!on) {
        m_daemon->stop();
        pause();
        setStatus(QStringLiteral("setup"), QStringLiteral("YouTube Music is turned off"));
        emit setupChanged();
        return;
    }
    m_daemon->start();
    setStatus(QStringLiteral("starting"), QStringLiteral("Starting YouTube Music"));
    emit setupChanged();
}

// One JSON object per line each way; replies carry the request's id
void YouTubeSource::request(const QJsonObject &req, const Reply &reply)
{
    QProcess *p = m_daemon->process();
    if (!p || p->state() != QProcess::Running) {
        if (reply)
            reply({ { QStringLiteral("ok"), false }, { QStringLiteral("error"), QStringLiteral("YouTube Music is not running") } });
        return;
    }
    QJsonObject r = req;
    const int id = m_next++;
    r.insert(QStringLiteral("id"), id);
    m_waiting.insert(id, reply);
    p->write(QJsonDocument(r).toJson(QJsonDocument::Compact) + '\n');
}

void YouTubeSource::received(const QString &line)
{
    if (!line.startsWith(QLatin1Char('{'))) {
        qCDebug(lcStream).noquote() << "godwit-ytmusic:" << line;
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(line.toUtf8()).object();
    const QString event = o.value(QLatin1String("event")).toString();
    if (event == QLatin1String("signedIn")) {
        m_signedIn = true;
        m_account = o.value(QLatin1String("account")).toString();
        m_pairing.clear();
        setStatus(QStringLiteral("ready"), QStringLiteral("Signed in"));
        emit message(QStringLiteral("Signed in to YouTube Music"));
        emit setupChanged();
        loadLists();
        return;
    }
    if (event == QLatin1String("signInFailed")) {
        m_pairing.clear();
        emit message(QStringLiteral("YouTube Music sign-in failed: ") + o.value(QLatin1String("error")).toString());
        emit setupChanged();
        return;
    }
    const Reply reply = m_waiting.take(o.value(QLatin1String("id")).toInt());
    if (!o.value(QLatin1String("ok")).toBool())
        qCWarning(lcStream).noquote() << "godwit-ytmusic:" << o.value(QLatin1String("error")).toString();
    if (reply)
        reply(o);
}

QList<QueueSource::Track> YouTubeSource::tracksOf(const QJsonValue &list)
{
    QList<Track> out;
    for (const QJsonValue &v : list.toArray()) {
        const QJsonObject t = v.toObject();
        Track tr;
        tr.key = t.value(QLatin1String("id")).toString();
        tr.title = t.value(QLatin1String("title")).toString();
        tr.artist = t.value(QLatin1String("artist")).toString();
        tr.album = t.value(QLatin1String("album")).toString();
        tr.art = t.value(QLatin1String("art")).toString();
        tr.dur = t.value(QLatin1String("dur")).toInt();
        if (!tr.key.isEmpty())
            out.append(tr);
    }
    return out;
}

// The browse panel: the last search's songs, then the account's playlists
void YouTubeSource::loadLists()
{
    if (!m_signedIn) {
        setLists(m_results);
        return;
    }
    request({ { QStringLiteral("cmd"), QStringLiteral("lists") } }, [this](const QJsonObject &r) {
        QVariantList lists = m_results;
        for (const QJsonValue &v : r.value(QLatin1String("items")).toArray())
            lists.append(v.toObject().toVariantMap());
        setLists(lists);
    });
}

void YouTubeSource::search(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    setBusy(true);
    request({ { QStringLiteral("cmd"), QStringLiteral("search") }, { QStringLiteral("q"), text.trimmed() } }, [this](const QJsonObject &r) {
        setBusy(false);
        if (!r.value(QLatin1String("ok")).toBool()) {
            emit message(QStringLiteral("YouTube Music search failed"));
            return;
        }
        m_results.clear();
        for (const QJsonValue &v : r.value(QLatin1String("items")).toArray()) {
            const QJsonObject t = v.toObject();
            m_results.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("song:") + t.value(QLatin1String("id")).toString() },
                                          { QStringLiteral("title"), t.value(QLatin1String("title")).toString() },
                                          { QStringLiteral("sub"), QStringLiteral("Song · ") + t.value(QLatin1String("artist")).toString() },
                                          { QStringLiteral("icon"), QStringLiteral("musical-note") } });
        }
        if (m_results.isEmpty())
            emit message(QStringLiteral("Nothing found"));
        loadLists();
    });
}

// The top song, with a radio of songs like it
void YouTubeSource::playSearch(const QString &text)
{
    setBusy(true);
    request({ { QStringLiteral("cmd"), QStringLiteral("search") }, { QStringLiteral("q"), text.trimmed() } }, [this, text](const QJsonObject &r) {
        setBusy(false);
        const QJsonArray items = r.value(QLatin1String("items")).toArray();
        if (!r.value(QLatin1String("ok")).toBool() || items.isEmpty()) {
            emit message(QStringLiteral("Nothing found on YouTube Music for ") + text);
            return;
        }
        const QJsonObject t = items.first().toObject();
        emit message(QStringLiteral("Playing ") + t.value(QLatin1String("title")).toString());
        openList(QStringLiteral("song:") + t.value(QLatin1String("id")).toString());
    });
}

// A song starts a radio of songs like it (YouTube Music's Up Next); a
// playlist plays from its start
void YouTubeSource::openList(const QString &id)
{
    setBusy(true);
    const bool song = id.startsWith(QLatin1String("song:"));
    QJsonObject req{ { QStringLiteral("cmd"), song ? QStringLiteral("radio") : QStringLiteral("tracks") },
                     { QStringLiteral("ref"), song ? id.mid(5) : id } };
    request(req, [this, song](const QJsonObject &r) {
        setBusy(false);
        const QList<Track> tracks = tracksOf(r.value(QLatin1String("tracks")));
        if (tracks.isEmpty()) {
            emit message(QStringLiteral("Nothing to play"));
            return;
        }
        const QString title = song ? QStringLiteral("Radio · ") + tracks.constFirst().title : r.value(QLatin1String("title")).toString();
        setQueue(tracks, title, 0);
    });
}

void YouTubeSource::play()
{
    if (tracks().isEmpty()) {
        if (!lists().isEmpty())
            openList(lists().constFirst().toMap().value(QStringLiteral("id")).toString());
        return;
    }
    QueueSource::play();
}

// Stream URLs expire after a few hours, so each is found as it is needed
void YouTubeSource::resolve(const Track &track, const std::function<void(const QUrl &)> &done)
{
    request({ { QStringLiteral("cmd"), QStringLiteral("stream") }, { QStringLiteral("ref"), track.key } }, [done](const QJsonObject &r) {
        done(r.value(QLatin1String("ok")).toBool() ? QUrl(r.value(QLatin1String("url")).toString()) : QUrl());
    });
}

QVariantMap YouTubeSource::setupInfo() const
{
    const bool on = m_settings.value(QStringLiteral("enabled"), true).toBool();
    QVariantList actions;
    if (m_signedIn)
        actions << action(QStringLiteral("signout"), QStringLiteral("Sign Out"));
    else if (!str(m_settings, "clientId").isEmpty() && !str(m_settings, "clientSecret").isEmpty())
        actions << action(QStringLiteral("signin"), QStringLiteral("Sign In"));
    actions << action(on ? QStringLiteral("disable") : QStringLiteral("enable"), on ? QStringLiteral("Turn Off") : QStringLiteral("Turn On"));
    return {
        { QStringLiteral("title"), QStringLiteral("YouTube Music") },
        { QStringLiteral("installed"), m_daemon->installed() },
        { QStringLiteral("enabled"), on },
        { QStringLiteral("signedIn"), m_signedIn },
        { QStringLiteral("account"), m_account },
        { QStringLiteral("pairing"), m_pairing },
        { QStringLiteral("fields"), QVariantList{
              field(QStringLiteral("clientId"), QStringLiteral("OAuth Client ID"), str(m_settings, "clientId"), false,
                    QStringLiteral("Ends in .apps.googleusercontent.com")),
              field(QStringLiteral("clientSecret"), QStringLiteral("OAuth Client Secret"), str(m_settings, "clientSecret"), true,
                    QStringLiteral("From the same OAuth client")) } },
        { QStringLiteral("actions"), actions },
        { QStringLiteral("steps"), QStringList{
              QStringLiteral("Searching and playing need no account, only an internet connection."),
              QStringLiteral("To use your playlists and liked songs, sign in. Google requires your own OAuth client: in Google Cloud Console, create a project, enable the YouTube Data API v3, and create an OAuth client ID of type \"TVs and Limited Input devices\"."),
              QStringLiteral("Enter its client ID and secret here, tap Sign In, then enter the code shown at google.com/device on a phone.") } },
    };
}

void YouTubeSource::applySettings(const QVariantMap &values)
{
    bool changed = false;
    for (const QString &k : { QStringLiteral("clientId"), QStringLiteral("clientSecret") }) {
        if (!values.contains(k))
            continue;
        const QString v = values.value(k).toString().trimmed();
        if (v != str(m_settings, k.toLatin1().constData())) {
            m_settings.insert(k, v);
            changed = true;
        }
    }
    if (!changed)
        return;
    saveSettings(id(), m_settings);
    emit setupChanged();
}

void YouTubeSource::signIn()
{
    request({ { QStringLiteral("cmd"), QStringLiteral("signin") },
              { QStringLiteral("clientId"), str(m_settings, "clientId") },
              { QStringLiteral("clientSecret"), str(m_settings, "clientSecret") } },
            [this](const QJsonObject &r) {
                if (!r.value(QLatin1String("ok")).toBool()) {
                    emit message(QStringLiteral("Could not start sign-in: ") + r.value(QLatin1String("error")).toString());
                    return;
                }
                m_pairing = { { QStringLiteral("url"), r.value(QLatin1String("url")).toString() },
                              { QStringLiteral("code"), r.value(QLatin1String("code")).toString() } };
                emit setupChanged();
            });
}

void YouTubeSource::signOut()
{
    request({ { QStringLiteral("cmd"), QStringLiteral("signout") } }, [this](const QJsonObject &) {
        m_signedIn = false;
        m_account.clear();
        setStatus(QStringLiteral("ready"), QStringLiteral("Search for music to play"));
        emit setupChanged();
        loadLists();
    });
}

// ---------------------------------------------------------------- Music

Music::Music(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(stateDir());
    QFile::setPermissions(stateDir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    m_folder = new MusicFolder(this);
    m_local = new LibrarySource(m_folder, false, this);
    m_usb = new LibrarySource(m_folder, true, this);
    m_spotify = new SpotifySource(this);
    m_apple = new AirPlaySource(this);
    m_youtube = new YouTubeSource(this);
    connect(m_spotify, &SpotifySource::setupChanged, this, &Music::setupChanged);
    connect(m_apple, &AirPlaySource::setupChanged, this, &Music::setupChanged);
    connect(m_youtube, &YouTubeSource::setupChanged, this, &Music::setupChanged);
}

MusicSource *Music::local() const { return m_local; }
MusicSource *Music::usb() const { return m_usb; }
MusicSource *Music::spotify() const { return m_spotify; }
MusicSource *Music::apple() const { return m_apple; }
MusicSource *Music::youtube() const { return m_youtube; }

MusicSource *Music::source(const QString &id) const
{
    if (id == QLatin1String("local"))
        return m_local;
    if (id == QLatin1String("usb"))
        return m_usb;
    if (id == QLatin1String("spotify"))
        return m_spotify;
    if (id == QLatin1String("apple"))
        return m_apple;
    if (id == QLatin1String("youtube"))
        return m_youtube;
    return nullptr;
}

// /var/lib/godwit on the head unit (systemd's StateDirectory=); the user's
// data folder elsewhere
QString Music::stateDir()
{
    const QString s = qEnvironmentVariable("STATE_DIRECTORY");
    return s.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : s.section(QLatin1Char(':'), 0, 0);
}

void Music::recheck(const QString &id)
{
    if (id == QLatin1String("spotify"))
        m_spotify->recheck();
    else if (id == QLatin1String("apple"))
        m_apple->recheck();
    else if (id == QLatin1String("youtube"))
        m_youtube->recheck();
}

QVariantMap Music::setup(const QString &id) const
{
    if (id == QLatin1String("spotify"))
        return m_spotify->setupInfo();
    if (id == QLatin1String("apple"))
        return m_apple->setupInfo();
    if (id == QLatin1String("youtube"))
        return m_youtube->setupInfo();
    return {};
}

void Music::saveSetup(const QString &id, const QVariantMap &values)
{
    if (id == QLatin1String("spotify"))
        m_spotify->applySettings(values);
    else if (id == QLatin1String("apple"))
        m_apple->applySettings(values);
    else if (id == QLatin1String("youtube"))
        m_youtube->applySettings(values);
}

void Music::setupAction(const QString &id, const QString &action)
{
    const bool enable = action == QLatin1String("enable");
    const bool disable = action == QLatin1String("disable");
    if (enable || disable) {
        QVariantMap s = loadSettings(id);
        s.insert(QStringLiteral("enabled"), enable);
        saveSettings(id, s);
    }
    if (id == QLatin1String("spotify")) {
        if (enable || disable)
            m_spotify->enable(enable);
        else if (action == QLatin1String("signout"))
            m_spotify->signOut();
    } else if (id == QLatin1String("apple")) {
        if (enable || disable)
            m_apple->enable(enable);
    } else if (id == QLatin1String("youtube")) {
        if (enable || disable)
            m_youtube->enable(enable);
        else if (action == QLatin1String("signin"))
            m_youtube->signIn();
        else if (action == QLatin1String("signout"))
            m_youtube->signOut();
    }
}
