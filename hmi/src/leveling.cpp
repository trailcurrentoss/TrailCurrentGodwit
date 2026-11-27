#include "leveling.h"

#include "music.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcLevel, "godwit.level")

namespace {
QString statePath() { return Music::stateDir() + QStringLiteral("/level.json"); }
constexpr double Deg = M_PI / 180.0;
}

Leveling::Leveling(QObject *parent)
    : QObject(parent)
{
    load();
}

QVariantMap Leveling::guide(double pitch, double roll) const
{
    QVariantMap g;
    if (!sized()) {
        g["level"] = false;
        g["steep"] = false;
        return g;
    }
    // Front above rear, and left above right, in mm at the wheels, as Plateau
    // works them out: nose up, the front is higher; leaning right, the left is
    const double fb = m_wheelbaseMm * std::tan(pitch * Deg);
    const double lr = m_trackMm * std::tan(roll * Deg);
    g["frontMm"] = std::max(0.0, -fb);
    g["rearMm"] = std::max(0.0, fb);
    g["leftMm"] = std::max(0.0, -lr);
    g["rightMm"] = std::max(0.0, lr);

    const double h[4] = { fb / 2 + lr / 2, fb / 2 - lr / 2, -fb / 2 + lr / 2, -fb / 2 - lr / 2 };
    const double top = *std::max_element(h, h + 4);
    QVariantList raise, pieces;
    double most = 0;
    const bool ramps = m_leveler == QLatin1String("ramps");
    for (double x : h) {
        const double r = top - x;
        most = std::max(most, r);
        raise.append(r);
        pieces.append(ramps || r < LevelMm ? 0 : piecesFor(r));
    }
    g["raiseMm"] = raise;
    g["pieces"] = pieces;
    g["level"] = most < LevelMm;
    g["steep"] = most > maxRaiseMm() + LevelMm;
    return g;
}

int Leveling::piecesFor(double mm) const
{
    return std::max(1, int(std::lround(mm / std::max(1, pieceMm()))));
}

double Leveling::rampAt(double mm) const
{
    // The ramps' top rises as sin(pi/2 * x / length) (assets/level/README.md)
    const double f = std::clamp(mm / std::max(1, m_rampMm), 0.0, 1.0);
    return std::asin(f) * 2 / M_PI;
}

// ---------------------------------------------------------------- settings

void Leveling::setLeveler(const QString &l)
{
    if (l == m_leveler || (l != QLatin1String("boards") && l != QLatin1String("blocks") && l != QLatin1String("ramps")))
        return;
    m_leveler = l;
    save();
    emit setupChanged();
}

void Leveling::setPieceMm(int mm)
{
    mm = std::clamp(mm, 5, 200);
    int &cur = m_leveler == QLatin1String("blocks") ? m_blockMm : m_boardMm;
    if (mm == cur)
        return;
    cur = mm;
    save();
    emit setupChanged();
}

#define LEVEL_SETTER(name, member, lo, hi)          \
    void Leveling::name(int v)                      \
    {                                               \
        v = std::clamp(v, lo, hi);                  \
        if (v == member)                            \
            return;                                 \
        member = v;                                 \
        save();                                     \
        emit setupChanged();                        \
    }
LEVEL_SETTER(setMaxPieces, m_maxPieces, 1, 10)
LEVEL_SETTER(setRampMm, m_rampMm, 10, 400)
LEVEL_SETTER(setWheelbaseMm, m_wheelbaseMm, 0, 15000)
LEVEL_SETTER(setTrackMm, m_trackMm, 0, 4000)
LEVEL_SETTER(setMounting, m_mounting, 0, 2)
#undef LEVEL_SETTER

void Leveling::load()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QString l = o.value("leveler").toString();
    if (l == QLatin1String("boards") || l == QLatin1String("blocks") || l == QLatin1String("ramps"))
        m_leveler = l;
    m_boardMm = o.value("boardMm").toInt(m_boardMm);
    m_blockMm = o.value("blockMm").toInt(m_blockMm);
    m_maxPieces = o.value("maxPieces").toInt(m_maxPieces);
    m_rampMm = o.value("rampMm").toInt(m_rampMm);
    m_wheelbaseMm = o.value("wheelbaseMm").toInt();
    m_trackMm = o.value("trackMm").toInt();
    m_mounting = o.value("mounting").toInt();
}

void Leveling::save()
{
    if (Music::stateDir().isEmpty())
        return;
    QJsonObject o;
    o["leveler"] = m_leveler;
    o["boardMm"] = m_boardMm;
    o["blockMm"] = m_blockMm;
    o["maxPieces"] = m_maxPieces;
    o["rampMm"] = m_rampMm;
    o["wheelbaseMm"] = m_wheelbaseMm;
    o["trackMm"] = m_trackMm;
    o["mounting"] = m_mounting;
    QDir().mkpath(Music::stateDir());
    QSaveFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        if (f.commit())
            return;
    }
    qCWarning(lcLevel) << "could not save" << statePath();
}
