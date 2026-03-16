#include "KeycardBridge.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QThread>
#include <QCoreApplication>

// C API from libkeycard.so (status-keycard-go)
extern "C" {
    // Session API (RPC)
    extern char* KeycardInitializeRPC(void);
    extern char* KeycardCallRPC(char* payload);
    extern void  KeycardSetSignalEventCallback(void* cb);
    extern void  ResetAPI(void);
    extern void  Free(void* param);

    // Flow API (atomic operations)
    extern char* KeycardInitFlow(char* storageDir);
    extern char* KeycardStartFlow(int flowType, char* jsonParams);
    extern char* KeycardResumeFlow(char* jsonParams);
    extern char* KeycardCancelFlow(void);
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

    QJsonValue errVal = response.value("error");
    if (!errVal.isNull() && !errVal.isUndefined()) {
        qWarning() << "keycard: start failed:" << errVal;
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

QJsonObject KeycardBridge::authorize(const QString &pin)
{
    QJsonObject params;
    params["pin"] = pin;

    QJsonObject response = rpcCall("keycard.Authorize", params);

    QJsonValue authErr = response.value("error");
    if (!authErr.isNull() && !authErr.isUndefined()) {
        qWarning() << "keycard: authorize RPC error:" << authErr;
        return {{"authorized", false}, {"error", authErr.toString()}};
    }

    QJsonObject result = response["result"].toObject();
    bool authorized = result["authorized"].toBool(false);

    if (authorized) {
        m_state = State::Authorized;
        emit stateChanged(m_state);
    } else {
        // Poll to get updated remaining attempts
        pollStatus();
    }

    QJsonObject out;
    out["authorized"] = authorized;
    out["remainingAttempts"] = m_remainingPIN;
    return out;
}

QByteArray KeycardBridge::exportKey(const QString &path)
{
    Q_UNUSED(path)

    // ExportLoginKeys returns encryption + whisper keys
    QJsonObject response = rpcCall("keycard.ExportLoginKeys");

    m_lastError.clear();

    QJsonValue exportErr = response.value("error");
    if (!exportErr.isNull() && !exportErr.isUndefined()) {
        m_lastError = exportErr.toString();
        qWarning() << "keycard: exportKey RPC error:" << m_lastError;
        return {};
    }

    // Response: {"result":{"keys":{"encryptionPrivateKey":{"privateKey":"hex",...},...}}}
    QJsonObject result = response["result"].toObject();

    QJsonObject keys = result["keys"].toObject();
    if (keys.isEmpty()) {
        m_lastError = "No keys. Full RPC response: " +
            QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)).left(500);
        qWarning() << "keycard:" << m_lastError;
        return {};
    }

    qDebug() << "keycard: available key types:" << keys.keys();
    QJsonObject encKey = keys["encryptionPrivateKey"].toObject();
    QString privKeyHex = encKey["privateKey"].toString();

    if (privKeyHex.isEmpty()) {
        // Try eip1581 as fallback
        QJsonObject eip = keys["eip1581"].toObject();
        privKeyHex = eip["privateKey"].toString();
        if (!privKeyHex.isEmpty())
            qDebug() << "keycard: using eip1581 key instead of encryptionPrivateKey";
    }

    if (privKeyHex.isEmpty()) {
        m_lastError = "No private key in exported keys: " + keys.keys().join(", ");
        qWarning() << "keycard:" << m_lastError;
        return {};
    }

    qDebug() << "keycard: exported encryption key," << privKeyHex.size() << "hex chars";
    return QByteArray::fromHex(privKeyHex.toUtf8());
}

QByteArray KeycardBridge::loginFlow(const QString &pin)
{
    // Stop Session API to avoid conflicts with Flow API
    // (both share the global Go context)
    if (m_running) {
        rpcCall("keycard.Stop");
        ResetAPI();
        m_running = false;
    }

    // Initialize Flow API with storage file path for pairings
    QString storageDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(storageDir);
    QString storageFile = storageDir + "/keycard_pairings";

    // Reset any previous flow result
    m_lastFlowResult = QJsonObject();

    // Keep QByteArray alive for the C call
    QByteArray storageBytes = storageFile.toUtf8();
    char *initResult = KeycardInitFlow(storageBytes.data());
    if (initResult) {
        QString res = QString::fromUtf8(initResult);
        qDebug() << "keycard: flow init:" << res;
        if (res != "ok") {
            m_lastError = "Flow init failed: " + res;
            Free(initResult);
            return {};
        }
        Free(initResult);
    }

    // Register signal callback
    KeycardSetSignalEventCallback(reinterpret_cast<void*>(&KeycardBridge::signalCallback));

    // Start Login flow (type 3) with PIN
    QJsonObject flowParams;
    flowParams["pin"] = pin;
    QByteArray paramsJson = QJsonDocument(flowParams).toJson(QJsonDocument::Compact);

    qDebug() << "keycard: starting login flow with storage:" << storageFile;
    char *startResult = KeycardStartFlow(3, paramsJson.data());
    if (startResult) {
        QString res = QString::fromUtf8(startResult);
        Free(startResult);
        if (res != "ok") {
            m_lastError = "Flow start failed: " + res;
            qWarning() << "keycard:" << m_lastError;
            return {};
        }
    }

    // Wait for flow-result signal (up to 10 seconds)
    // Go callback fires on a goroutine thread — use atomic flag for cross-thread visibility.
    m_flowResultReady.store(false);
    m_lastFlowResult = QJsonObject();

    for (int i = 0; i < 100; ++i) {
        QThread::msleep(100);

        if (m_flowResultReady.load()) {
            qDebug() << "keycard: flow result ready after" << (i * 100) << "ms";
            break;
        }
    }

    if (!m_flowResultReady.load()) {
        m_lastError = "Login flow timed out (10s)";
        KeycardCancelFlow();
        return {};
    }

    // Check for error
    QString error = m_lastFlowResult["error"].toString();
    if (!error.isEmpty()) {
        m_lastError = "Login flow error: " + error;
        return {};
    }

    // Extract encryption private key
    QJsonObject encKey = m_lastFlowResult["encryption-key"].toObject();
    QString privKeyHex = encKey["privateKey"].toString();

    if (privKeyHex.isEmpty()) {
        m_lastError = "No encryption key in flow result";
        return {};
    }

    // Also capture key UID
    m_keyUID = m_lastFlowResult["key-uid"].toString();

    qDebug() << "keycard: loginFlow exported encryption key," << privKeyHex.size() << "hex chars";
    m_state = State::Authorized;
    emit stateChanged(m_state);

    return QByteArray::fromHex(privKeyHex.toUtf8());
}

void KeycardBridge::pollStatus()
{
    // Always attempt RPC — m_running may have been set in a previous callModule invocation
    QJsonObject response = rpcCall("keycard.GetStatus");

    QJsonValue errVal = response.value("error");
    if (!errVal.isNull() && !errVal.isUndefined()) {
        qDebug() << "keycard: pollStatus error:" << errVal;
        return;
    }

    QJsonObject result = response["result"].toObject();
    QString stateStr = result["state"].toString();

    if (stateStr.isEmpty()) {
        qDebug() << "keycard: pollStatus empty state, response:" << response;
        return;
    }

    // If we got a valid status, the RPC server is running
    m_running = true;

    // Extract card info
    QJsonObject kcStatus = result["keycardStatus"].toObject();
    if (!kcStatus.isEmpty()) {
        m_remainingPIN = kcStatus["remainingAttemptsPIN"].toInt(-1);
        m_remainingPUK = kcStatus["remainingAttemptsPUK"].toInt(-1);
        m_keyInitialized = kcStatus["keyInitialized"].toBool(false);
    }

    QJsonObject kcInfo = result["keycardInfo"].toObject();
    if (!kcInfo.isEmpty()) {
        m_keyUID = kcInfo["keyUID"].toString();
    }

    State newState = parseState(stateStr);
    if (newState != m_state) {
        m_state = newState;
        qDebug() << "keycard: polled state:" << stateStr << "->" << static_cast<int>(newState);
        emit stateChanged(newState);
    }
}

QJsonObject KeycardBridge::rpcCall(const QString &method, const QJsonObject &params)
{
    QJsonObject request;
    request["jsonrpc"] = "2.0";
    request["id"] = QString::number(++m_rpcId);
    request["method"] = method;

    // Always include params — Go RPC requires "params":[{}] even for no-arg methods.
    QJsonArray paramsArray;
    paramsArray.append(params.isEmpty() ? QJsonObject() : params);
    request["params"] = paramsArray;

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
    } else if (type == "keycard.flow-result") {
        s_instance->m_lastFlowResult = event["event"].toObject();
        s_instance->m_flowResultReady.store(true);
        qDebug() << "keycard: flow result signal received";
    }
}

KeycardBridge::State KeycardBridge::parseState(const QString &stateStr)
{
    // Normalize: Go uses both "waitingForCard" (signals) and "waiting-for-card" (GetStatus)
    QString s = stateStr.toLower().remove('-');

    if (s == "nopcsc" || s == "nopcsc")    return State::NoPCSC;
    if (s == "waitingforreader")           return State::WaitingForReader;
    if (s == "waitingforcard")             return State::WaitingForCard;
    if (s == "connectingcard")             return State::ConnectingCard;
    if (s == "connectionerror")            return State::ConnectionError;
    if (s == "notkeycard")                 return State::NotKeycard;
    if (s == "emptykeycard")               return State::EmptyKeycard;
    if (s == "blockedpin")                 return State::BlockedPIN;
    if (s == "blockedpuk")                 return State::BlockedPUK;
    if (s == "ready")                      return State::Ready;
    if (s == "authorized")                 return State::Authorized;
    return State::Unknown;
}
