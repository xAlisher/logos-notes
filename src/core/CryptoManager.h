#pragma once

#include <QByteArray>
#include <QString>

// Wraps libsodium AEAD encryption and Argon2id key derivation.
// Cipher is chosen once at account creation (import) and persisted
// in the DB. Existing accounts always use the persisted cipher,
// regardless of current hardware capabilities.
class CryptoManager
{
public:
    static constexpr int KEY_BYTES = 32;

    enum Cipher { AES256GCM, XCHACHA20POLY1305 };

    CryptoManager();

    // Set cipher explicitly (called after loading from DB meta).
    void setCipher(Cipher c);

    // Detect best cipher for this hardware (used only for new accounts).
    static Cipher detectBestCipher();

    Cipher cipher() const { return m_cipher; }
    int    nonceBytes() const;
    int    aBytes() const;

    // String ↔ enum for DB persistence.
    static QString cipherToString(Cipher c);
    static Cipher  cipherFromString(const QString &s);

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
