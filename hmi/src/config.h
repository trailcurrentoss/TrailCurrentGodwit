#pragma once

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

private:
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
