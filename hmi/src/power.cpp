#include "power.h"

#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPower, "godwit.power")

namespace {
constexpr int GiveUpMs = 15000;
}

Power::Power(QObject *parent)
    : QObject(parent)
{
    m_giveUp.setSingleShot(true);
    m_giveUp.setInterval(GiveUpMs);
    connect(&m_giveUp, &QTimer::timeout, this, [this] {
        qCWarning(lcPower) << "still running after asking" << m_request;
        QFile::remove(m_request);
        emit restartFailed();
    });
}

void Power::restart()
{
    request(QStringLiteral("restart"));
}

void Power::factoryReset()
{
    request(QStringLiteral("factory-reset"));
}

void Power::request(const QString &name)
{
    if (QGuiApplication::platformName() != QLatin1String("eglfs")) {
        qCInfo(lcPower) << "not doing" << name << ": this is not the head unit";
        return;
    }
    qCInfo(lcPower) << name;
    m_request = qEnvironmentVariable("XDG_RUNTIME_DIR") + QLatin1Char('/') + name;
    QFile f(m_request);
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcPower) << "could not ask for" << name << ":" << f.errorString();
        emit restartFailed();
        return;
    }
    m_giveUp.start();
}
