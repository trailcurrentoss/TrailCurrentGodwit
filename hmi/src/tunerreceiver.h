#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class QProcess;

// An app that shows what the RTL-SDR dongle hears, by running a decoder from
// [../radio] while it is open. The decoder prints a line each for what it hears;
// the subclass turns those into the list.
//
// There is one dongle, shared with the radio: see tunerHeld.
//
// Used on the head unit (eglfs) only, or with GODWIT_RADIO=1. Elsewhere available
// stays false and the app shows the simulator's demo data.
class TunerReceiver : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(bool available READ available CONSTANT)
    // Whether to run the decoder
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // It has the dongle, or is still letting go of it. Nothing else may open the
    // dongle until this goes false.
    Q_PROPERTY(bool tunerHeld READ tunerHeld NOTIFY tunerHeldChanged)
    // False while the dongle is missing
    Q_PROPERTY(bool tunerFound READ tunerFound NOTIFY stateChanged)
    // It has opened the dongle and is listening
    Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)
    // What has been heard: the fields are the subclass's, and absent until sent.
    // Nothing stays here once the decoder stops, so this is all recent.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)

public:
    ~TunerReceiver() override;

    bool available() const { return m_available; }
    bool active() const { return m_active; }
    void setActive(bool active);
    bool tunerHeld() const { return m_held; }
    bool tunerFound() const { return m_tunerFound; }
    bool listening() const { return m_listening; }
    QVariantList list() const;

signals:
    void activeChanged();
    void tunerHeldChanged();
    void stateChanged();
    void listChanged();

protected:
    // name for the log; binVariable overrides defaultBin, where the decoder is
    TunerReceiver(const char *name, const char *binVariable, const QString &defaultBin, QObject *parent);

    // A line from the decoder that is not one of those common to them all:
    // ready, error, rate and gone <key>. True if the list changed.
    virtual bool handleLine(const QByteArray &line) = 0;

    QHash<QByteArray, QVariantMap> m_items;   // by what the decoder calls each

private:
    void start();
    void stop();
    void readDecoder();
    void noteRate(int frames);
    void updateHeld();
    void forget();

    const char *m_name;
    bool m_available = false;
    bool m_active = false;
    bool m_held = false;
    bool m_tunerFound = true;
    bool m_listening = false;
    QString m_bin;
    QProcess *m_decoder = nullptr;
    // One still letting go of the dongle
    QProcess *m_exiting = nullptr;
    QByteArray m_buffer;
    int m_rateLines = 0;
    int m_frames = 0;
    QTimer m_retry;                           // looks for the dongle again
};
