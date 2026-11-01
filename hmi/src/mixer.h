#pragma once

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

class QProcess;

// Volume and mute per audio role, through the system PipeWire. AGL's
// WirePlumber policy routes each kind of sound through a role sink
// (input.loopback.sink.role.<role>): "multimedia" for music and radio,
// "navigation" for guidance, "communication" for calls. Setting a role's
// volume leaves the others alone.
//
// Uses wpctl, which ships with WirePlumber. Changes are coalesced, so a knob
// spun quickly runs wpctl a few times, not once per detent.
//
// Used on the head unit (eglfs) only, or with GODWIT_MIXER=1.
class Mixer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit Mixer(QObject *parent = nullptr);

    bool available() const { return m_available; }

    // percent 0..100
    Q_INVOKABLE void setVolume(const QString &role, int percent);
    Q_INVOKABLE void setMuted(const QString &role, bool muted);

signals:
    // The role sinks appeared or were recreated, at full volume: at boot, or
    // when WirePlumber restarts, as a deploy does. Every volume and mute has
    // to be set again.
    void sinksReset();

private:
    struct Want { int volume = -1; int muted = -1; };

    void flush();
    void run(const QStringList &args);
    void resolve();
    void watch();
    QString idFor(const QString &role) const;

    bool m_available = false;
    QHash<QString, Want> m_want;     // waiting to be applied
    QHash<QString, QString> m_ids;   // role -> PipeWire node id
    QHash<QString, QString> m_seen;  // the role sinks the watch saw last
    QStringList m_queue;             // one wpctl command per entry, space separated
    QProcess *m_proc = nullptr;
    QProcess *m_watchProc = nullptr;
    QTimer m_timer;
    QTimer m_watchTimer;
};
