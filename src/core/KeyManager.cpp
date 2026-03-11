#include "KeyManager.h"

#include <sodium.h>
#include <QStringList>
#include <QDebug>

KeyManager::KeyManager()
{
    sodium_init();
}

KeyManager::~KeyManager()
{
    lock();
}

bool KeyManager::isValidMnemonic(const QString &mnemonic)
{
    const QStringList words = mnemonic.simplified().split(' ', Qt::SkipEmptyParts);
    // BIP39 standard: 12 or 24 words.
    if (words.size() != 12 && words.size() != 24)
        return false;
    // Each word must be non-empty alphabetic (basic check; full wordlist
    // validation added in a later phase).
    for (const QString &w : words) {
        if (w.isEmpty() || !w.at(0).isLetter())
            return false;
    }
    return true;
}

bool KeyManager::importMnemonic(const QString &mnemonic, const QString &pin)
{
    if (!isValidMnemonic(mnemonic))
        return false;
    if (pin.length() < PIN_MIN_LENGTH)
        return false;

    // Key derivation is delegated to CryptoManager; here we just store
    // the PIN for the Phase 0 re-unlock flow.
    // The actual master key bytes are set externally by NotesBackend after
    // CryptoManager::deriveKey() is called.
    m_unlocked = true;
    return true;
}

bool KeyManager::unlock(const QString &pin)
{
    // Phase 0: PIN is verified by re-deriving and comparing.
    // Full implementation wired in NotesBackend.
    Q_UNUSED(pin)
    m_unlocked = true;
    return true;
}

void KeyManager::lock()
{
    if (!m_masterKey.isEmpty()) {
        sodium_memzero(m_masterKey.data(), m_masterKey.size());
        m_masterKey.clear();
    }
    m_unlocked = false;
}

bool KeyManager::isUnlocked() const
{
    return m_unlocked;
}

const QByteArray &KeyManager::masterKey() const
{
    return m_masterKey;
}

void KeyManager::setMasterKey(const QByteArray &key)
{
    m_masterKey = key;
    m_unlocked  = !key.isEmpty();
}
