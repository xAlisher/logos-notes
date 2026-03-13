#pragma once

#include <QByteArray>
#include <QString>

// Wraps libsodium AEAD encryption and Argon2id key derivation.
// Automatically selects AES-256-GCM (if hardware AES-NI available)
// or XChaCha20-Poly1305 as fallback.
class CryptoManager
{
public:
    static constexpr int KEY_BYTES = 32;

    enum Cipher { AES256GCM, XCHACHA20POLY1305 };

    CryptoManager();

    Cipher cipher() const { return m_cipher; }
    int    nonceBytes() const;
    int    aBytes() const;

    // Derive a 256-bit master key from a BIP39 mnemonic using Argon2id.
    QByteArray deriveKey(const QString &mnemonic,
                         const QByteArray &salt) const;

    // Derive a 256-bit wrapping key from a PIN + caller-supplied random salt.
    QByteArray deriveKeyFromPin(const QString &pin,
                                const QByteArray &salt) const;

    static QByteArray randomSalt();

    // Encrypt plaintext with selected AEAD cipher. Returns ciphertext; fills nonce.
    QByteArray encrypt(const QByteArray &plaintext,
                       const QByteArray &key,
                       QByteArray       &nonceOut) const;

    // Decrypt ciphertext with selected AEAD cipher. Returns plaintext, or empty on failure.
    QByteArray decrypt(const QByteArray &ciphertext,
                       const QByteArray &key,
                       const QByteArray &nonce) const;

private:
    Cipher m_cipher;
};
