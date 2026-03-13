#pragma once

#include <QByteArray>
#include <QString>

// Wraps libsodium: AES-256-GCM encryption and Argon2id key derivation.
class CryptoManager
{
public:
    static constexpr int KEY_BYTES  = 32; // crypto_aead_aes256gcm_KEYBYTES
    static constexpr int NONCE_BYTES = 12; // crypto_aead_aes256gcm_NPUBBYTES

    CryptoManager();

    // Derive a 256-bit master key from a BIP39 mnemonic using Argon2id.
    // Caller must supply a random salt (16 bytes) that is persisted in the DB.
    // Returns empty QByteArray on failure.
    QByteArray deriveKey(const QString &mnemonic,
                         const QByteArray &salt) const;

    // Derive a 256-bit wrapping key from a PIN + caller-supplied random salt.
    // The salt must be crypto_pwhash_SALTBYTES (16) bytes and stored in the DB
    // alongside the wrapped key so it can be reproduced on unlock.
    // Returns empty QByteArray on failure.
    QByteArray deriveKeyFromPin(const QString &pin,
                                const QByteArray &salt) const;

    // Generate a fresh random salt suitable for deriveKeyFromPin().
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
};
