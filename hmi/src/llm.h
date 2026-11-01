#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

// Understanding spoken commands the HMI's own patterns miss, with Llama 3.2 1B
// on the Hexagon NPU (godwit-llm.service, 127.0.0.1:11434: /api/chat, with
// example turns and the answer begun as JSON). Nothing leaves the head unit.
//
// interpret() answers with understood(command), command being one of
//   {action: "directions", place}
//   {action: "call", contact}
//   {action: "text", contact, message}
//   {action: "radio", band, frequency} or {action: "radio", preset} or {action: "radio", station}
//   {action: "none"}
// as the model wrote it; the HMI checks it before acting. Or failed(reason):
// "unavailable", "timeout" or "error".
//
// Used on the head unit (eglfs) only, or with GODWIT_LLM=1. GODWIT_LLM_URL
// overrides the server.
class Llm : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // The server has answered; it loads the model onto the NPU after boot
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit Llm(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool busy() const { return !m_reply.isNull(); }

    // A new request replaces one still running
    Q_INVOKABLE void interpret(const QString &text);
    Q_INVOKABLE void cancel();

signals:
    void availableChanged();
    void busyChanged();
    void understood(const QVariantMap &command);
    void failed(const QString &reason);

private:
    void probe();
    void finished(QNetworkReply *reply);
    void setAvailable(bool available);

    bool m_wanted = false;
    bool m_available = false;
    QString m_base;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    // Asks the server whether it is up until it is
    QTimer m_probe;
};
