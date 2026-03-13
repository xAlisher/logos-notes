#pragma once

#include <QByteArray>
#include <QString>

// Wraps libsodium: AES-256-GCM encryption and Argon2id key derivation.
// Requires AES-NI hardware — fails fast at construction if unavailable.
class CryptoManager
{
public:
    static constexpr int KEY_BYTES   = 32; // crypto_aead_aes256gcm_KEYBYTES
    static constexpr int NONCE_BYTES = 12; // crypto_aead_aes256gcm_NPUBBYTES

    CryptoManager();

    // Returns true if AES-256-GCM hardware is available.
    bool isAvailable() const { return m_available; }

    // Derive a 256-bit master key from a BIP39 mnemonic using Argon2id.
    QByteArray deriveKey(const QString &mnemonic,
                         const QByteArray &salt) const;

    // Derive a 256-bit wrapping key from a PIN + caller-supplied random salt.
    QByteArray deriveKeyFromPin(const QString &pin,
                                const QByteArray &salt) const;

    static QByteArray randomSalt();

    // Encrypt plaintext with AES-256-GCM. Returns ciphertext; fills nonce.
    QByteArray encrypt(const QByteArray &plaintext,
                       const QByteArray &key,
                       QByteArray       &nonceOut) const;

    // Decrypt ciphertext with AES-256-GCM. Returns plaintext, or empty on failure.
    QByteArray decrypt(const QByteArray &ciphertext,
                       const QByteArray &key,
                       const QByteArray &nonce) const;

private:
    bool m_available = false;
};
