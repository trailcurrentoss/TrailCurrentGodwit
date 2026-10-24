#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>

// Install locations, overridable from the environment for desktop runs.
//
//   GODWIT_MAP_STYLE_DIR  map styles, glyphs and sprites (godwit-map-style)
//                         default /usr/share/godwit/maps
//   GODWIT_MAP_DATA       the active map bundle's tiles
//                         default /data/maps/current/tiles.pmtiles
//
// The installed styles already point at the default tiles path; the tiles
// path is exposed so the map page can tell when no map bundle is installed.
// Also hands QML the license texts built into the app, for About & Legal,
// and remembers the driver safety notice's acceptance for the rest of this
// boot: a marker in XDG_RUNTIME_DIR (/run/godwit on the head unit, kept
// across HMI restarts, gone at power off).
class Config : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString mapStyleLight READ mapStyleLight CONSTANT)
    Q_PROPERTY(QString mapStyleDark READ mapStyleDark CONSTANT)
    Q_PROPERTY(bool mapInstalled READ mapInstalled CONSTANT)

public:
    using QObject::QObject;

    QString mapStyleLight() const { return style(QStringLiteral("3d")); }
    QString mapStyleDark() const { return style(QStringLiteral("3d-dark")); }
    bool mapInstalled() const
    {
        return QFileInfo::exists(env("GODWIT_MAP_DATA", "/data/maps/current/tiles.pmtiles"));
    }
    Q_INVOKABLE bool noticeAccepted() const { return QFileInfo::exists(noticeMarker()); }
    Q_INVOKABLE void acceptNotice() const
    {
        QFile f(noticeMarker());
        if (!f.open(QIODevice::WriteOnly))
            qWarning("Could not record the safety notice as accepted in %s", qPrintable(f.fileName()));
    }
    // An SPDX id's text from assets/licenses, empty if it is not built in
    Q_INVOKABLE QString licenseText(const QString &id) const
    {
        QFile f(QStringLiteral(":/assets/licenses/") + id + QStringLiteral(".txt"));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    }

private:
    static QString noticeMarker()
    {
        return qEnvironmentVariable("XDG_RUNTIME_DIR", QDir::tempPath()) + QStringLiteral("/notice-accepted");
    }
    static QString env(const char *name, const char *fallback)
    {
        const QString v = qEnvironmentVariable(name);
        return v.isEmpty() ? QString::fromLatin1(fallback) : v;
    }
    static QString style(const QString &name)
    {
        const QString dir = env("GODWIT_MAP_STYLE_DIR", "/usr/share/godwit/maps");
        return QUrl::fromLocalFile(dir + QStringLiteral("/styles/") + name + QStringLiteral("/style.json")).toString();
    }
};
