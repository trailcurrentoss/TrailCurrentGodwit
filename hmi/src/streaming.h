#pragma once

#include "music.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

// Settings kept for a source in the HMI's state folder: <id>.json, readable
// by the HMI only, since some hold secrets
QVariantMap loadSettings(const QString &id);
void saveSettings(const QString &id, const QVariantMap &values);

// A helper program the HMI keeps running while its source is turned on:
// restarted when it exits (after a pause that grows with each failure), and
// stopped with the HMI
class Daemon : public QObject
{
    Q_OBJECT

public:
    Daemon(const QString &name, QObject *parent);
    ~Daemon() override;

    // Finds the program on PATH or under the environment variable's path.
    // Looks again on each start(), so a helper installed later is found
    // when its source is turned on, without restarting the HMI.
    bool installed() const { return !m_program.isEmpty(); }
    void setProgram(const char *envVar, const QString &fallback);
    void findProgram();
    void setArguments(const QStringList &args) { m_args = args; }
    void start();
    void stop();
    void restart();
    bool running() const { return m_proc && m_proc->state() != QProcess::NotRunning; }
    QProcess *process() const { return m_proc; }

signals:
    void started();
    void exited(int code);
    void line(const QString &text);

private:
    void launch();

    QString m_name;
    const char *m_envVar = nullptr;
    QString m_fallback;
    QString m_program;
    QStringList m_args;
    QProcess *m_proc = nullptr;
    QTimer m_retry;
    int m_failures = 0;
    bool m_wanted = false;
};

// Spotify through go-librespot, a Spotify Connect player run by the HMI with
// its config and stored login in <state>/spotify. Its API on 127.0.0.1 plays,
// pauses and skips, and lists the account's playlists and their tracks.
//
// Browse offers a search of Spotify's catalogue (songs, artists, albums and
// playlists, through the Web API), then the account's playlists and Liked
// Songs. A song or an artist can start a radio station of music like it.
// Search uses the driver's own Spotify app (client ID and secret, by the
// client credentials flow) when one is set, else go-librespot's session
// token, which Spotify rate-limits heavily. A 429 is honoured until its
// Retry-After; meanwhile spoken requests fall back to the account's
// playlists and Liked Songs.
//
// Signing in uses Spotify's device pairing: go-librespot asks Spotify for a
// code, the setup page shows it, and the driver enters it at spotify.com/pair
// on a phone. Needs Spotify Premium. Once signed in, the head unit also shows
// up in the Spotify app's device list, so music can be sent to it from a phone.
//
// Off until turned on in its settings, so the head unit only contacts Spotify
// once the driver asks it to.
// GODWIT_SPOTIFY_BIN overrides the program (default go-librespot on PATH).
class SpotifySource : public MusicSource
{
    Q_OBJECT

public:
    explicit SpotifySource(QObject *parent);

    QVariantList queue() const override { return m_queue; }
    int queueIndex() const override { return m_queueIndex; }
    QString queueTitle() const override { return m_queueTitle; }
    bool hasQueue() const override { return true; }
    bool canSearch() const override { return true; }

    void play() override;
    void pause() override;
    void next() override;
    void previous() override;
    void playAt(int index) override;
    void setShuffle(bool on) override;
    void openList(const QString &id) override;
    void search(const QString &text) override;
    void playSearch(const QString &text) override;

    // For the setup page
    QVariantMap setupInfo() const;
    void applySettings(const QVariantMap &values);
    void signOut();
    void enable(bool on);
    // Starts it again as its settings say, finding a helper installed since
    void recheck() { enable(m_settings.value(QStringLiteral("enabled"), false).toBool()); }

signals:
    void setupChanged();

private:
    QString dir() const;
    void writeConfig();
    void poll();
    void status(const QJsonObject &s);
    void loadPlaylists();
    void loadContext(const QString &uri);
    void showLists();
    void accessToken(const std::function<void(const QString &, const QString &)> &done);
    void webSearch(const QString &text, const std::function<void(const QVariantList &, const QString &)> &done);
    QString searchProblem(const QString &error) const;
    void playFromLibrary(const QString &text, const QString &problem, int tries);
    static QVariantMap browseRow(const QString &id, const QString &title, const QString &sub, const QString &icon);
    void post(const QString &path, const QJsonObject &body = {}, const QString &failed = QString());
    QNetworkReply *get(const QString &path);

    Daemon *m_daemon;
    QNetworkAccessManager *m_net;
    QTimer m_poll;
    QVariantMap m_settings;
    QString m_user;
    QVariantMap m_pairing;      // {url, code} while waiting for the driver
    QVariantList m_playlists;   // the account's playlists, then Liked Songs
    QVariantList m_results;     // the last search, offered first in Browse
    int m_contextTries = 0;     // listings of the playing context asked for
    QString m_appToken;         // the driver's own Spotify app's, for search
    qint64 m_appTokenUntil = 0;
    qint64 m_searchBlockedUntil = 0;  // Spotify said to wait (429) until then
    int m_retryAfter = 0;
    QString m_context;          // the playlist or album playing
    QString m_trackUri;
    QVariantList m_queue;
    QStringList m_queueUris;
    QString m_queueTitle;
    int m_queueIndex = -1;
    bool m_wasPlaying = false;
    bool m_listed = false;
    int m_misses = 0;
};

// Apple Music, from an iPhone: shairport-sync makes the head unit an AirPlay
// receiver, and the phone sends its music over Wi-Fi (the two on the same
// network). Its MPRIS interface on the system bus gives what is playing and
// passes play, pause and skip back to the phone. Apple has no way to play
// Apple Music on the head unit itself.
//
// The receiver's name and an optional password are its settings.
// GODWIT_AIRPLAY_BIN overrides the program (default shairport-sync on PATH);
// GODWIT_AIRPLAY_BUS=session looks for it on the session bus, for testing.
class AirPlaySource : public MusicSource
{
    Q_OBJECT

public:
    explicit AirPlaySource(QObject *parent);

    void play() override { call(QStringLiteral("Play")); }
    void pause() override { call(QStringLiteral("Pause")); }
    void next() override { call(QStringLiteral("Next")); }
    void previous() override { call(QStringLiteral("Previous")); }

    QVariantMap setupInfo() const;
    void applySettings(const QVariantMap &values);
    void enable(bool on);
    void recheck() { enable(m_settings.value(QStringLiteral("enabled"), true).toBool()); }

signals:
    void setupChanged();

private slots:
    void propertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void writeConfig();
    void call(const QString &method);
    void refresh();
    void apply(const QVariantMap &props);

    Daemon *m_daemon;
    QDBusConnection m_bus;
    QVariantMap m_settings;
    QString m_conf;
    bool m_wasPlaying = false;
};

// YouTube Music through godwit-ytmusic ([../../ytmusic]), a helper that
// searches and lists with ytmusicapi and finds each song's audio stream with
// yt-dlp; the HMI plays the stream itself. Search and play work without an
// account. Signing in (for the account's playlists and liked songs) needs the
// driver's own Google OAuth client for "TVs and Limited Input devices": its
// client ID and secret are entered on the setup page, then a code at
// google.com/device on a phone.
//
// GODWIT_YTMUSIC_BIN overrides the helper (default godwit-ytmusic on PATH).
class YouTubeSource : public QueueSource
{
    Q_OBJECT

public:
    explicit YouTubeSource(QObject *parent);

    bool canSearch() const override { return true; }
    void play() override;
    void openList(const QString &id) override;
    void search(const QString &text) override;
    void playSearch(const QString &text) override;

    QVariantMap setupInfo() const;
    void applySettings(const QVariantMap &values);
    void signIn();
    void signOut();
    void enable(bool on);
    void recheck() { enable(m_settings.value(QStringLiteral("enabled"), true).toBool()); }

signals:
    void setupChanged();

protected:
    void resolve(const Track &track, const std::function<void(const QUrl &)> &done) override;

private:
    using Reply = std::function<void(const QJsonObject &)>;
    void request(const QJsonObject &req, const Reply &reply = {});
    void received(const QString &line);
    void loadLists();
    static QList<Track> tracksOf(const QJsonValue &list);

    Daemon *m_daemon;
    QVariantMap m_settings;
    QHash<int, Reply> m_waiting;
    int m_next = 1;
    QByteArray m_buffer;
    bool m_signedIn = false;
    QString m_account;
    QVariantMap m_pairing;      // {url, code} while waiting for the driver
    QVariantList m_results;     // the last search, offered as a list
};
