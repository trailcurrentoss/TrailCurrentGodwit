#include "logs.h"

#include "music.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcLogs, "godwit.logs")

namespace {

const QString Settings = QStringLiteral("logging");
const QString Verbose = QStringLiteral("verbose");

bool savedVerbose()
{
    return loadSettings(Settings).value(Verbose).toBool();
}

void applyRules(bool verbose)
{
    QLoggingCategory::setFilterRules(verbose ? QStringLiteral("godwit.*.debug=true")
                                             : QStringLiteral("godwit.*.debug=false"));
}

} // namespace

Logs::Logs(QObject *parent)
    : QObject(parent)
    , m_verbose(savedVerbose())
{
}

void Logs::applySaved()
{
    applyRules(savedVerbose());
    qCDebug(lcLogs) << "verbose logging is on";
}

void Logs::setVerbose(bool verbose)
{
    if (verbose == m_verbose)
        return;
    m_verbose = verbose;
    QDir().mkpath(Music::stateDir());
    saveSettings(Settings, { { Verbose, verbose } });
    applyRules(verbose);
    qCInfo(lcLogs) << (verbose ? "verbose logging on" : "verbose logging off");
    if (!verbose)
        clear();
    emit verboseChanged();
}

void Logs::clear()
{
    // Not the journal of a desktop running the simulator
    if (QGuiApplication::platformName() != QLatin1String("eglfs"))
        return;
    QFile f(qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/clear-logs"));
    if (!f.open(QIODevice::WriteOnly))
        qCWarning(lcLogs) << "could not clear the logs:" << f.errorString();
}
