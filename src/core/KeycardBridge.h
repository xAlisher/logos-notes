#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <atomic>

// Thin C++ wrapper around libkeycard.so (status-keycard-go compiled via CGO).
// Manages PC/SC reader monitoring and card state via JSON-RPC.
class KeycardBridge : public QObject
{
    Q_OBJECT

public:
    // Card detection states (mirrors status-keycard-go state machine)
    enum class State {
        Unknown,            // Before start() called
        NoPCSC,             // PC/SC library not available
        WaitingForReader,   // No USB reader connected
        WaitingForCard,     // Reader connected, no card inserted
        ConnectingCard,     // Establishing connection to card
        ConnectionError,    // Communication error
        NotKeycard,         // Card present but not a Keycard
        EmptyKeycard,       // Uninitialized Keycard (no mnemonic loaded)
        BlockedPIN,         // PIN blocked (0 attempts left)
        BlockedPUK,         // PUK blocked (card bricked)
        Ready,              // Card connected, ready for PIN
        Authorized,         // PIN verified, card unlocked
    };
    Q_ENUM(State)

    explicit KeycardBridge(QObject *parent = nullptr);
    ~KeycardBridge() override;

    // Start PC/SC monitoring. Returns true on success.
    bool start();

    // Stop monitoring and release resources.
    void stop();

    // Current card state.
    State state() const { return m_state; }

    // Human-readable status text for the current state.
    QString statusText() const;

    // Whether the bridge is actively monitoring.
    bool isRunning() const { return m_running; }

    // Actively query the Go RPC for current state (updates cached state).
    void pollStatus();

    // Authorize with PIN. Returns JSON: {"authorized":true} or {"authorized":false,"remainingAttempts":N}
    QJsonObject authorize(const QString &pin);

    // Export key at derivation path. Returns raw private key bytes.
    // Default path: m/43'/60'/1581' (EIP-1581 encryption root)
    QByteArray exportKey(const QString &path = "m/43'/60'/1581'");

    // Flow API: Login (auth + export in one atomic operation).
    // Returns the encryption private key bytes, or empty on failure.
    // This avoids the mutex bug in Session API's ExportLoginKeys.
    QByteArray loginFlow(const QString &pin);

    // Last error from an RPC call (for debugging)
    QString lastError() const { return m_lastError; }

    // Card info from last pollStatus (remaining attempts, key UID, etc.)
    int remainingPINAttempts() const { return m_remainingPIN; }
    int remainingPUKAttempts() const { return m_remainingPUK; }
    bool keyInitialized() const { return m_keyInitialized; }
    QString keyUID() const { return m_keyUID; }

signals:
    void stateChanged(KeycardBridge::State newState);

private:
    // Send a JSON-RPC call to libkeycard and return the parsed response.
    QJsonObject rpcCall(const QString &method, const QJsonObject &params = {});

    // Process signal events from the Go library.
    static void signalCallback(const char *jsonEvent);

    // Parse state string from signal into enum.
    static State parseState(const QString &stateStr);

    State m_state = State::Unknown;
    bool m_running = false;
    int m_rpcId = 0;

    QString m_lastError;
    QJsonObject m_lastFlowResult;
    std::atomic<bool> m_flowResultReady{false};

    // Card status from GetStatus responses
    int m_remainingPIN = -1;
    int m_remainingPUK = -1;
    bool m_keyInitialized = false;
    QString m_keyUID;

    // Singleton for signal callback routing (Go callback is C function pointer)
    static KeycardBridge *s_instance;
};
