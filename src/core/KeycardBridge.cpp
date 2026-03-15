#include "KeycardBridge.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

// C API from libkeycard.so (status-keycard-go)
extern "C" {
    extern char* KeycardInitializeRPC(void);
    extern char* KeycardCallRPC(char* payload);
    extern void  KeycardSetSignalEventCallback(void* cb);
    extern void  ResetAPI(void);
    extern void  Free(void* param);
}

KeycardBridge *KeycardBridge::s_instance = nullptr;

KeycardBridge::KeycardBridge(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
}

KeycardBridge::~KeycardBridge()
{
    stop();
    if (s_instance == this)
        s_instance = nullptr;
}

bool KeycardBridge::start()
{
    if (m_running)
        return true;

    // Initialize the RPC server
    char *initResult = KeycardInitializeRPC();
    if (initResult) {
        qDebug() << "keycard: RPC initialized:" << initResult;
        Free(initResult);
    }

    // Register signal callback
    KeycardSetSignalEventCallback(reinterpret_cast<void*>(&KeycardBridge::signalCallback));

    // Start PC/SC monitoring
    QString storageDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(storageDir);
    QString pairingsPath = storageDir + "/keycard_pairings.json";

    QJsonObject params;
    params["storageFilePath"] = pairingsPath;
    params["logEnabled"] = false;

    QJsonObject response = rpcCall("keycard.Start", params);

    if (response.contains("error")) {
        qWarning() << "keycard: start failed:" << response["error"];
        m_state = State::NoPCSC;
        emit stateChanged(m_state);
        return false;
    }

    m_running = true;
    m_state = State::WaitingForReader;
    emit stateChanged(m_state);
    return true;
}

void KeycardBridge::stop()
{
    if (!m_running)
        return;

    rpcCall("keycard.Stop");
    ResetAPI();
    m_running = false;
    m_state = State::Unknown;
    emit stateChanged(m_state);
}

QString KeycardBridge::statusText() const
{
    switch (m_state) {
    case State::Unknown:          return QStringLiteral("Not started");
    case State::NoPCSC:           return QStringLiteral("PC/SC service not available");
    case State::WaitingForReader: return QStringLiteral("Connect a USB card reader");
    case State::WaitingForCard:   return QStringLiteral("Insert your Keycard");
    case State::ConnectingCard:   return QStringLiteral("Connecting...");
    case State::ConnectionError:  return QStringLiteral("Connection error — reinsert card");
    case State::NotKeycard:       return QStringLiteral("Not a Keycard — use a Status Keycard");
    case State::EmptyKeycard:     return QStringLiteral("Card not initialized — set up with Keycard Shell first");
    case State::BlockedPIN:       return QStringLiteral("PIN blocked — use PUK to unblock");
    case State::BlockedPUK:       return QStringLiteral("Card permanently blocked");
    case State::Ready:            return QStringLiteral("Keycard detected — enter PIN");
    case State::Authorized:       return QStringLiteral("Keycard unlocked");
    }
    return QStringLiteral("Unknown state");
}

bool KeycardBridge::authorize(const QString &pin)
{
    QJsonObject params;
    params["pin"] = pin;

    QJsonObject response = rpcCall("keycard.Authorize", params);

    if (response.contains("error")) {
        qWarning() << "keycard: authorize failed:" << response["error"];
        return false;
    }

    // State will be updated via signal callback to Authorized
    return true;
}

QByteArray KeycardBridge::exportKey(const QString &path)
{
    Q_UNUSED(path)

    // ExportRecoverKeys returns master, wallet, and EIP-1581 keys
    QJsonObject response = rpcCall("keycard.ExportRecoverKeys");

    if (response.contains("error")) {
        qWarning() << "keycard: exportKey failed:" << response["error"];
        return {};
    }

    // The response contains key material — extract the EIP-1581 encryption key
    QJsonObject result = response["result"].toObject();
    QString encKeyHex = result["encryptionPrivateKey"].toString();

    if (encKeyHex.isEmpty()) {
        qWarning() << "keycard: no encryption key in export response";
        return {};
    }

    return QByteArray::fromHex(encKeyHex.toUtf8());
}

QJsonObject KeycardBridge::rpcCall(const QString &method, const QJsonObject &params)
{
    QJsonObject request;
    request["jsonrpc"] = "2.0";
    request["id"] = QString::number(++m_rpcId);
    request["method"] = method;

    if (!params.isEmpty()) {
        QJsonArray paramsArray;
        paramsArray.append(params);
        request["params"] = paramsArray;
    }

    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);

    char *result = KeycardCallRPC(payload.data());
    if (!result)
        return {{"error", "null response from KeycardCallRPC"}};

    QJsonObject response = QJsonDocument::fromJson(result).object();
    Free(result);

    return response;
}

void KeycardBridge::signalCallback(const char *jsonEvent)
{
    if (!s_instance || !jsonEvent)
        return;

    QJsonObject event = QJsonDocument::fromJson(jsonEvent).object();
    QString type = event["type"].toString();

    if (type == "status-changed") {
        QString stateStr = event["event"].toObject()["state"].toString();
        State newState = parseState(stateStr);

        if (newState != s_instance->m_state) {
            s_instance->m_state = newState;
            qDebug() << "keycard: state changed to" << stateStr;
            emit s_instance->stateChanged(newState);
        }
    }
}

KeycardBridge::State KeycardBridge::parseState(const QString &stateStr)
{
    if (stateStr == "noPCSC")              return State::NoPCSC;
    if (stateStr == "waitingForReader")    return State::WaitingForReader;
    if (stateStr == "waitingForCard")      return State::WaitingForCard;
    if (stateStr == "connectingCard")      return State::ConnectingCard;
    if (stateStr == "connectionError")     return State::ConnectionError;
    if (stateStr == "notKeycard")          return State::NotKeycard;
    if (stateStr == "emptyKeycard")        return State::EmptyKeycard;
    if (stateStr == "blockedPIN")          return State::BlockedPIN;
    if (stateStr == "blockedPUK")          return State::BlockedPUK;
    if (stateStr == "ready")               return State::Ready;
    if (stateStr == "authorized")          return State::Authorized;
    return State::Unknown;
}
