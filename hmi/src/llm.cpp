#include "llm.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>

Q_LOGGING_CATEGORY(lcLlm, "godwit.llm")

namespace {

// A 1B model follows example turns far better than rules: the examples are
// earlier turns whose answers are bare JSON, and the answer is begun for it,
// so it writes one object and the server stops it at the closing brace.
// Spelling out each action's shape steers it away from "text" for anything
// it does not recognise. Measured on the NPU: 1 to 3 seconds per request.
const char *const SystemPrompt =
    "You turn a driver's spoken request into one JSON command for a car head unit.\n"
    "Going somewhere, finding a place or navigating: {\"action\":\"directions\",\"place\":\"...\"}\n"
    "Phoning someone: {\"action\":\"call\",\"contact\":\"...\"}\n"
    "Texting or messaging someone: {\"action\":\"text\",\"contact\":\"...\",\"message\":\"...\"}\n"
    "Radio by frequency, preset or station: {\"action\":\"radio\",\"band\":\"FM\",\"frequency\":94.7}, "
    "{\"action\":\"radio\",\"preset\":3}, {\"action\":\"radio\",\"station\":\"...\"}\n"
    "Stopping the route or navigation: {\"action\":\"endroute\"}\n"
    "Anything else, including questions: {\"action\":\"none\"}\n"
    "Use text only when the driver asks to send a message.";

// {request, answer}; "none" examples spread through, against a lean to the
// last one shown
const char *const Examples[][2] = {
    { "I need to get to the Safeway in Moab", R"({"action":"directions","place":"Safeway Moab"})" },
    { "Ring my brother Tom", R"({"action":"call","contact":"Tom"})" },
    { "What's the weather like", R"({"action":"none"})" },
    { "Tell Sarah I'm running late", R"({"action":"text","contact":"Sarah","message":"I'm running late"})" },
    { "Put on one oh six point seven", R"({"action":"radio","band":"FM","frequency":106.7})" },
    { "Take me home", R"({"action":"directions","place":"home"})" },
    { "Tell me a joke", R"({"action":"none"})" },
    { "Shoot a message over to Priya", R"({"action":"text","contact":"Priya","message":""})" },
    { "Go to my third radio preset", R"({"action":"radio","preset":3})" },
    { "Is it going to rain later", R"({"action":"none"})" },
    { "Listen to KTRL", R"({"action":"radio","station":"KTRL"})" },
    { "Find a gas station near Green River", R"({"action":"directions","place":"gas station Green River"})" },
    { "How tall is Mount Whitney", R"({"action":"none"})" },
    { "We don't need the directions anymore", R"({"action":"endroute"})" },
};

const char *const Prefill = R"({"action":")";

// The first complete {...} in what the model wrote. Small models sometimes
// add words before or after it.
QJsonObject firstObject(const QString &text)
{
    const int start = text.indexOf(QLatin1Char('{'));
    if (start < 0)
        return {};
    int depth = 0;
    bool quoted = false, escaped = false;
    for (int i = start; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (escaped) {
            escaped = false;
        } else if (c == QLatin1Char('\\')) {
            escaped = quoted;
        } else if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && c == QLatin1Char('{')) {
            ++depth;
        } else if (!quoted && c == QLatin1Char('}') && --depth == 0) {
            return QJsonDocument::fromJson(text.mid(start, i - start + 1).toUtf8()).object();
        }
    }
    return {};
}

} // namespace

Llm::Llm(QObject *parent)
    : QObject(parent)
    , m_base(qEnvironmentVariable("GODWIT_LLM_URL", QStringLiteral("http://127.0.0.1:11434")))
    , m_net(new QNetworkAccessManager(this))
{
    m_wanted = qEnvironmentVariableIntValue("GODWIT_LLM") == 1
        || QGuiApplication::platformName() == QLatin1String("eglfs");
    m_probe.setInterval(10000);
    connect(&m_probe, &QTimer::timeout, this, &Llm::probe);
    if (m_wanted) {
        m_probe.start();
        probe();
    }
}

void Llm::probe()
{
    QNetworkRequest req(QUrl(m_base + QStringLiteral("/")));
    req.setTransferTimeout(3000);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setAvailable(reply->error() == QNetworkReply::NoError);
    });
}

void Llm::setAvailable(bool available)
{
    if (available == m_available)
        return;
    m_available = available;
    qCInfo(lcLlm) << (available ? "language model ready" : "language model not answering");
    if (available)
        m_probe.stop();
    else if (m_wanted)
        m_probe.start();
    emit availableChanged();
}

void Llm::interpret(const QString &text)
{
    cancel();
    if (!m_available) {
        emit failed(QStringLiteral("unavailable"));
        return;
    }
    auto turn = [](const char *role, const QString &content) {
        return QJsonObject { { QStringLiteral("role"), QLatin1String(role) }, { QStringLiteral("content"), content } };
    };
    QJsonArray messages { turn("system", QString::fromUtf8(SystemPrompt)) };
    for (const auto &ex : Examples)
        messages << turn("user", QString::fromUtf8(ex[0])) << turn("assistant", QString::fromUtf8(ex[1]));
    messages << turn("user", text);
    const QJsonObject body {
        { QStringLiteral("messages"), messages },
        { QStringLiteral("prefill"), QLatin1String(Prefill) },
        { QStringLiteral("stop"), QJsonArray { QStringLiteral("}") } },
    };
    QNetworkRequest req(QUrl(m_base + QStringLiteral("/api/chat")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(20000);
    qCInfo(lcLlm) << "interpret" << text;
    m_reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finished(reply); });
    emit busyChanged();
}

void Llm::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->abort();
    emit busyChanged();
}

void Llm::finished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply)
        return;
    m_reply = nullptr;
    emit busyChanged();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(lcLlm) << "request failed:" << reply->errorString();
        const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError
            || reply->error() == QNetworkReply::TimeoutError;
        if (!timedOut)
            setAvailable(false);
        emit failed(timedOut ? QStringLiteral("timeout") : QStringLiteral("error"));
        return;
    }
    const QString answer = QJsonDocument::fromJson(reply->readAll()).object()
                               .value(QLatin1String("response")).toString();
    const QJsonObject command = firstObject(answer);
    qCInfo(lcLlm).noquote() << "answer" << answer.simplified();
    if (command.isEmpty() || !command.contains(QLatin1String("action"))) {
        emit understood(QVariantMap { { QStringLiteral("action"), QStringLiteral("none") } });
        return;
    }
    emit understood(command.toVariantMap());
}
