#pragma once

#include "library.h"

#include <QAudioOutput>
#include <QHash>
#include <QMediaPlayer>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// One place music comes from in the Media app: the head unit's own library,
// a USB drive, a streaming service. Each keeps what it is playing and its
// queue; the HMI chooses which one is heard (Hmi.audio) and pauses the rest.
//
// status says whether it can play:
//   "ready"    it can (it may still have nothing queued)
//   "setup"    it needs settings entered first (Settings in the Media app)
//   "starting" its service is starting or signing in
//   "error"    it failed; note says why
//   "off"      it is not installed on this head unit
// note is one line for the user about the status.
//
// track is {title, artist, album, art (URL or ""), dur (seconds, 0 unknown)}.
// The position is in seconds as of positionAt (ms since the epoch); while
// playing, the page counts on from there, as it does for the phone.
// queue is [{title, artist, dur}], with queueIndex the one playing.
// lists are what can be browsed: [{id, title, sub}]; openList() plays one.
class MusicSource : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString note READ note NOTIFY statusChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QVariantMap track READ track NOTIFY trackChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double positionAt READ positionAt NOTIFY positionChanged)
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY queueChanged)
    Q_PROPERTY(QString queueTitle READ queueTitle NOTIFY queueChanged)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY shuffleChanged)
    Q_PROPERTY(QVariantList lists READ lists NOTIFY listsChanged)
    // What the screen offers: a queue it can show and play from, search
    Q_PROPERTY(bool hasQueue READ hasQueue CONSTANT)
    Q_PROPERTY(bool canSearch READ canSearch CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    QString id() const { return m_id; }
    QString name() const { return m_name; }
    QString status() const { return m_status; }
    QString note() const { return m_note; }
    bool playing() const { return m_playing; }
    QVariantMap track() const { return m_track; }
    double position() const { return m_position; }
    double positionAt() const { return m_positionAt; }
    virtual QVariantList queue() const { return {}; }
    virtual int queueIndex() const { return -1; }
    virtual QString queueTitle() const { return {}; }
    bool shuffle() const { return m_shuffle; }
    QVariantList lists() const { return m_lists; }
    virtual bool hasQueue() const { return false; }
    virtual bool canSearch() const { return false; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE virtual void play() = 0;
    Q_INVOKABLE virtual void pause() = 0;
    Q_INVOKABLE void toggle() { if (m_playing) pause(); else play(); }
    Q_INVOKABLE virtual void next() = 0;
    Q_INVOKABLE virtual void previous() = 0;
    Q_INVOKABLE virtual void playAt(int index) { Q_UNUSED(index) }
    Q_INVOKABLE virtual void setShuffle(bool on) { Q_UNUSED(on) }
    Q_INVOKABLE virtual void openList(const QString &id) { Q_UNUSED(id) }
    Q_INVOKABLE virtual void search(const QString &text) { Q_UNUSED(text) }
    // Plays the best match for what was asked for ("Hotel California",
    // "Eagles", "Road Trip"), for the voice assistant. message() says when
    // nothing matched.
    Q_INVOKABLE virtual void playSearch(const QString &text) { Q_UNUSED(text) }

signals:
    void statusChanged();
    void playingChanged();
    void trackChanged();
    void positionChanged();
    void queueChanged();
    void shuffleChanged();
    void listsChanged();
    void busyChanged();
    // Music started from elsewhere (a phone casting to the head unit): the
    // HMI makes this the source it plays
    void startedElsewhere();
    // Something to tell the driver in a toast
    void message(const QString &text);

protected:
    MusicSource(const QString &id, const QString &name, QObject *parent);
    void setStatus(const QString &status, const QString &note = QString());
    void setPlaying(bool playing);
    void setTrack(const QVariantMap &track);
    void setPosition(double seconds);
    void setShuffleFlag(bool on);
    void setLists(const QVariantList &lists);
    void setBusy(bool busy);

private:
    QString m_id;
    QString m_name;
    QString m_status = QStringLiteral("starting");
    QString m_note;
    bool m_playing = false;
    QVariantMap m_track;
    double m_position = 0;
    double m_positionAt = 0;
    bool m_shuffle = false;
    QVariantList m_lists;
    bool m_busy = false;
};

// A source the head unit plays itself, from a queue of tracks: the library,
// a USB drive, YouTube Music. Plays through Qt Multimedia (FFmpeg) into the
// multimedia role sink. The queue wraps around at its end; shuffle reorders
// what follows the playing track and turning it off puts the order back.
class QueueSource : public MusicSource
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    struct Track {
        QString key;     // the file path, or the service's id for the track
        QUrl url;        // empty until resolve() finds it
        QString title;
        QString artist;
        QString album;
        QString art;
        int dur = 0;     // seconds
        int number = 0;  // on its album
    };

    QVariantList queue() const override;
    int queueIndex() const override { return m_index; }
    QString queueTitle() const override { return m_queueTitle; }
    bool hasQueue() const override { return true; }

    void play() override;
    void pause() override;
    void next() override;
    void previous() override;
    void playAt(int index) override;
    void setShuffle(bool on) override;

protected:
    QueueSource(const QString &id, const QString &name, QObject *parent);
    // Replaces the queue; with start >= 0 that track plays at once
    void setQueue(const QList<Track> &tracks, const QString &title, int start = -1);
    // Updates tracks already queued (their tags became known) without
    // interrupting the one playing
    void refreshQueue(const QHash<QString, Track> &byKey);
    const QList<Track> &tracks() const { return m_tracks; }
    // Finds where a track's sound is, when url is empty; calls done(url),
    // or done({}) when it cannot be played
    virtual void resolve(const Track &track, const std::function<void(const QUrl &)> &done) { Q_UNUSED(track) done({}); }
    void stop();

private:
    void load(int index, bool play);
    void showTrack();
    void pickOutput();

    QList<Track> m_tracks;
    QList<Track> m_unshuffled;
    int m_index = -1;
    QString m_queueTitle;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_output = nullptr;
    QMediaDevices *m_devices = nullptr;
    bool m_wantPlay = false;
    int m_loading = 0;     // drops a resolve() overtaken by another track
    int m_failures = 0;    // tracks in a row that would not play
};

// /data/media/music, the head unit's own music, brought in from USB drives
// like photos and videos. Also lists the music on a drive that is plugged in
// (driveTracks), which plays straight from the drive without importing.
//
// Each file's tags (title, artist, album, track number, length, and the cover
// from the file or a cover.jpg beside it) are read once with TagLib, on a
// worker thread, and kept in the cache folder. Built without TagLib, titles
// come from the file names ("Artist - Title") and lengths stay unknown.
// GODWIT_MUSIC_DIR overrides the folder.
class MusicFolder : public MediaFolder
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit MusicFolder(QObject *parent = nullptr);

    QList<QueueSource::Track> libraryTracks() const;
    QList<QueueSource::Track> driveTracks() const;
    QHash<QString, QueueSource::Track> known() const;

signals:
    void libraryChanged();
    void driveChanged();
    // Tags for some files became known
    void tagsRead();

protected:
    void driveFiles(const QStringList &files) override;

private:
    QueueSource::Track trackFor(const QString &path) const;
    void readTags(const QStringList &paths);
    void loadIndex();
    void saveIndex();

    QString m_cache;
    QHash<QString, QueueSource::Track> m_tags;   // path -> what its tags say
    QStringList m_drive;
    QStringList m_pending;
    bool m_reading = false;
};

// Plays the library (local) or a drive (usb), browsed as All Songs, by album
// and by artist
class LibrarySource : public QueueSource
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    LibrarySource(MusicFolder *folder, bool usb, QObject *parent);

    void play() override;
    void openList(const QString &id) override;
    void playSearch(const QString &text) override;

private:
    QList<Track> all() const;
    void rebuild();

    MusicFolder *m_folder;
    bool m_usb;
};

class SpotifySource;
class AirPlaySource;
class YouTubeSource;

// The Media app's sources, and the settings each one needs (Settings in the
// Media app, while parked). Settings are kept in the HMI's state folder
// (/var/lib/godwit, STATE_DIRECTORY), readable by the HMI only.
class Music : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(MusicSource *local READ local CONSTANT)
    Q_PROPERTY(MusicSource *usb READ usb CONSTANT)
    Q_PROPERTY(MusicSource *spotify READ spotify CONSTANT)
    Q_PROPERTY(MusicSource *apple READ apple CONSTANT)
    Q_PROPERTY(MusicSource *youtube READ youtube CONSTANT)
    // The library folder, for importing from USB
    Q_PROPERTY(MediaFolder *library READ library CONSTANT)

public:
    explicit Music(QObject *parent = nullptr);

    MusicSource *local() const;
    MusicSource *usb() const;
    MusicSource *spotify() const;
    MusicSource *apple() const;
    MusicSource *youtube() const;
    MediaFolder *library() const { return m_folder; }

    // A source by id: local, usb, spotify, apple, youtube
    Q_INVOKABLE MusicSource *source(const QString &id) const;

    // The settings the Media app's setup page shows for a source:
    // [{key, label, value, secret, hint}] plus actions it offers
    Q_INVOKABLE QVariantMap setup(const QString &id) const;
    Q_INVOKABLE void saveSetup(const QString &id, const QVariantMap &values);
    // An action on the setup page: "signin", "signout", "enable", "disable"
    Q_INVOKABLE void setupAction(const QString &id, const QString &action);
    // Looks again for a source's helper program, when it was not installed
    Q_INVOKABLE void recheck(const QString &id);

    static QString stateDir();

signals:
    void setupChanged();

private:
    MusicFolder *m_folder;
    LibrarySource *m_local;
    LibrarySource *m_usb;
    SpotifySource *m_spotify;
    AirPlaySource *m_apple;
    YouTubeSource *m_youtube;
};
