#pragma once

#include <QObject>
#include <QQmlEngine>

// How much the HMI logs, set in Settings > System. Normally it is what happened,
// without what anyone said, their messages or which phone. Verbose adds those
// and more detail, for troubleshooting, and stays on until it is turned off.
// Turning it off clears the system's logs: the journal holds them for every
// service and only root may clear it, so it leaves a file for godwit-clear-logs.path.
class Logs : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool verbose READ verbose WRITE setVerbose NOTIFY verboseChanged)

public:
    explicit Logs(QObject *parent = nullptr);

    // Before anything logs, at start-up
    static void applySaved();

    bool verbose() const { return m_verbose; }
    void setVerbose(bool verbose);

signals:
    void verboseChanged();

private:
    void clear();

    bool m_verbose = false;
};
