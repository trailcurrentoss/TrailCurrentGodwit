#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

// Restarts the head unit, or erases everything personal on it. The HMI's user
// may not do either, so it leaves a file for godwit-restart.path or
// godwit-factory-reset.path. Only on the head unit (eglfs): the simulator must not
// touch the desktop it runs on.
class Power : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Power(QObject *parent = nullptr);

    Q_INVOKABLE void restart();
    Q_INVOKABLE void factoryReset();

signals:
    // Still running a while after asking
    void restartFailed();

private:
    void request(const QString &name);

    QString m_request;
    QTimer m_giveUp;
};
