#include "mixer.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcMix, "godwit.mixer")

namespace {

QProcessEnvironment wpctlEnv()
{
    // The system-wide PipeWire's socket, not a per-user one
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PIPEWIRE_RUNTIME_DIR"), QStringLiteral("/run/pipewire"));
    return env;
}

} // namespace

Mixer::Mixer(QObject *parent)
    : QObject(parent)
{
    const bool wanted = qEnvironmentVariableIntValue("GODWIT_MIXER") == 1
        || QGuiApplication::platformName() == QLatin1String("eglfs");
    m_available = wanted && !QStandardPaths::findExecutable(QStringLiteral("wpctl")).isEmpty();
    m_timer.setSingleShot(true);
    m_timer.setInterval(60);
    connect(&m_timer, &QTimer::timeout, this, &Mixer::flush);
}

void Mixer::setVolume(const QString &role, int percent)
{
    if (!m_available)
        return;
    m_want[role].volume = qBound(0, percent, 100);
    m_timer.start();
}

void Mixer::setMuted(const QString &role, bool muted)
{
    if (!m_available)
        return;
    m_want[role].muted = muted ? 1 : 0;
    m_timer.start();
}

// Node ids of the role sinks, from "wpctl status -n"
void Mixer::resolve()
{
    QProcess p;
    p.setProcessEnvironment(wpctlEnv());
    p.start(QStringLiteral("wpctl"), { QStringLiteral("status"), QStringLiteral("-n") });
    if (!p.waitForFinished(3000)) {
        qCWarning(lcMix) << "wpctl status did not finish";
        return;
    }
    static const QRegularExpression re(QStringLiteral("(\\d+)\\.\\s+input\\.loopback\\.sink\\.role\\.(\\S+)"));
    m_ids.clear();
    auto it = re.globalMatch(QString::fromUtf8(p.readAllStandardOutput()));
    while (it.hasNext()) {
        const auto m = it.next();
        m_ids.insert(m.captured(2), m.captured(1));
    }
    qCInfo(lcMix) << "role sinks" << m_ids;
}

QString Mixer::idFor(const QString &role) const
{
    return m_ids.value(role);
}

void Mixer::flush()
{
    for (auto it = m_want.cbegin(); it != m_want.cend(); ++it) {
        if (idFor(it.key()).isEmpty())
            resolve();
        const QString id = idFor(it.key());
        if (id.isEmpty()) {
            qCWarning(lcMix) << "no PipeWire sink for role" << it.key();
            continue;
        }
        if (it->volume >= 0)
            m_queue << QStringLiteral("set-volume %1 %2").arg(id).arg(it->volume / 100.0, 0, 'f', 2);
        if (it->muted >= 0)
            m_queue << QStringLiteral("set-mute %1 %2").arg(id).arg(it->muted);
    }
    m_want.clear();
    if (!m_proc && !m_queue.isEmpty())
        run(m_queue.takeFirst().split(QLatin1Char(' ')));
}

void Mixer::run(const QStringList &args)
{
    m_proc = new QProcess(this);
    m_proc->setProcessEnvironment(wpctlEnv());
    connect(m_proc, &QProcess::finished, this, [this, args](int code) {
        if (code != 0) {
            // PipeWire restarted, or the node went away: look the ids up again
            qCWarning(lcMix) << "wpctl" << args << "failed:" << m_proc->readAllStandardError().trimmed();
            m_ids.clear();
        }
        m_proc->deleteLater();
        m_proc = nullptr;
        if (!m_queue.isEmpty())
            run(m_queue.takeFirst().split(QLatin1Char(' ')));
    });
    m_proc->start(QStringLiteral("wpctl"), args);
}
