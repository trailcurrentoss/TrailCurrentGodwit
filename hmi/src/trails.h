#pragma once

#include "library.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// GPX tracks for the map (/data/media/trails, for the Trails app). They come
// from USB drives through MediaFolder, or are recorded while driving.
//
// Each file is read once on a worker thread. A file's <trk> and <trkseg> parts
// stay separate, so no line is drawn across the gaps between them; its <rte>
// are used only when it has no tracks, as they are usually a sparse copy of
// them. Points are thinned to what the map can show.
//
// Rows add to MediaFolder's {url, n}: file (the row's key), color, on (shown on
// the map), mi, gainM (metres of climb, 0 when the file has too little
// elevation data to say), parts, pts, west/south/east/north, ready (the file
// has been read), bad (it holds no track) and rec (it was recorded here).
//
// What the user chose (name, color, on) is in .trails.json in the same folder.
// The GPX files are left as they came, except that naming a recorded track also
// writes the name into its file.
//
// A recording goes point by point into .recording.gpx, synced every few
// seconds as the head unit can lose power at any moment. Only the record button
// ends one: if the power goes first, as when the engine is switched off for
// fuel, the next start carries on in the same file, in a new <trkseg> so no
// line is drawn across the stop, and sets resumed.
//
// GODWIT_TRAILS_DIR overrides the folder.
class Trails : public MediaFolder
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // GeoJSON FeatureCollection of the tracks that are on, each with a color
    // property. Bytes rather than an object: the map engine parses them
    // itself, on no thread that holds up the screen, and a source's data has to
    // reach it as either bytes or a JavaScript object.
    Q_PROPERTY(QByteArray overlay READ overlay NOTIFY overlayChanged)
    Q_PROPERTY(int onCount READ onCount NOTIFY overlayChanged)
    // The colors offered, chosen to read on both the day and night maps. None
    // is close to the green of the active route.
    Q_PROPERTY(QStringList colors READ colors CONSTANT)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(double recordedMiles READ recordedMiles NOTIFY recordedChanged)
    // GeoJSON bytes of the recording so far, a point every so often
    Q_PROPERTY(QByteArray recordedLine READ recordedLine NOTIFY recordedLineChanged)
    // A recording the power cut off has carried on from where it was
    Q_PROPERTY(bool resumed READ resumed CONSTANT)

public:
    explicit Trails(QObject *parent = nullptr);

    QByteArray overlay() const { return m_overlay; }
    int onCount() const { return m_onCount; }
    QStringList colors() const;
    bool recording() const { return m_recFile.isOpen(); }
    double recordedMiles() const { return m_recMiles; }
    QByteArray recordedLine() const;
    bool resumed() const { return m_resumed; }

    Q_INVOKABLE void setOn(const QString &file, bool on);
    Q_INVOKABLE void setColor(const QString &file, const QString &color);
    Q_INVOKABLE void rename(const QString &file, const QString &name);
    Q_INVOKABLE void remove(const QString &file);
    // For a small drawing of the track: {w, h, parts: [[x, y, x, y ...] ...]},
    // north up, with x and y within w and h
    Q_INVOKABLE QVariantMap shape(const QString &file) const;
    // The track's point nearest to a position: {lat, lon, mi}, or {} if none
    Q_INVOKABLE QVariantMap nearest(const QString &file, double lat, double lon) const;

    Q_INVOKABLE void startRecording();
    // A fix from the receiver. Fixes with no real movement are not kept.
    Q_INVOKABLE void addFix(double lat, double lon, double altitude, const QDateTime &utc);
    // Ends the recording and saves it as a track called name. Returns its file,
    // or "" when too little was recorded to be a track, which is then discarded.
    Q_INVOKABLE QString stopRecording(const QString &name);

    struct Point { double lat, lon; };
    struct Parsed {
        QString name;
        QList<QList<Point>> parts;   // thinned
        double miles = 0;
        double gainM = 0;
        int points = 0;              // as in the file
        double west = 0, south = 0, east = 0, north = 0;
    };
    // Reads a GPX file. Whatever came before an error in the file is kept.
    static Parsed parse(const QString &path);

signals:
    void overlayChanged();
    void recordingChanged();
    void recordedChanged();
    void recordedLineChanged();

protected:
    void decorate(QVariantMap &row, const QFileInfo &file) override;

private:
    QString recordingPath() const;
    QString saveRecording(const QString &name);
    void resume();
    void next();
    void rebuildOverlay();
    QString nextColor() const;
    void adopt(const QString &file);
    void loadSettings();
    void saveSettings();
    void writeNameInto(const QString &file, const QString &name);

    QHash<QString, Parsed> m_parsed;       // file name -> what it holds
    QJsonObject m_settings;                // file name -> {name, color, on, rec}
    QString m_busy;
    QByteArray m_overlay;
    int m_onCount = 0;
    bool m_resumed = false;

    QFile m_recFile;
    QElapsedTimer m_sinceSync;
    QElapsedTimer m_sinceFix;
    bool m_recHasFix = false;
    Point m_recLast{ 0, 0 };
    Point m_recLineLast{ 0, 0 };
    int m_recPoints = 0;
    double m_recMiles = 0;
    QList<Point> m_recLine;
};
