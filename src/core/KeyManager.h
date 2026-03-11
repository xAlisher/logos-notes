#pragma once

#include <QByteArray>
#include <QString>

// Handles BIP39 mnemonic validation and in-memory session key management.
// The master key is derived from the mnemonic and optionally protected by PIN.
// Phase 0: PIN gates access to the in-memory key (no hardware key).
class KeyManager
{
public:
    static constexpr int PIN_MIN_LENGTH = 4;

    KeyManager();
    ~KeyManager();

    // Returns true if the mnemonic is a valid BIP39 phrase (12 or 24 words).
    static bool isValidMnemonic(const QString &mnemonic);

    // Derive and store the master key from mnemonic + PIN.
    // The key is kept in memory; the PIN is not stored anywhere.
    // Returns false if mnemonic is invalid.
    bool importMnemonic(const QString &mnemonic, const QString &pin);

    // Unlock: re-derive the master key from mnemonic + PIN.
    // In Phase 0 the mnemonic is not re-entered on unlock;
    // this overload is used internally after deriving the key on import.
    bool unlock(const QString &pin);

    // Wipe the master key from memory.
    void lock();

    bool isUnlocked() const;

    // Access the raw key bytes (only valid when unlocked).
    const QByteArray &masterKey() const;

    // Set the master key directly (called by NotesBackend after derivation).
    void setMasterKey(const QByteArray &key);

private:
    QByteArray m_masterKey;
    QByteArray m_derivedKey; // key encrypted by PIN, stored for re-unlock
    bool       m_unlocked = false;
};
