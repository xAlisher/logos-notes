#include "CryptoManager.h"
#include "SecureBuffer.h"

#include <sodium.h>
#include <QDebug>

CryptoManager::CryptoManager()
{
    if (sodium_init() < 0) {
        // -1 means failure; 1 means already initialised — both fine here.
    }

    m_available = crypto_aead_aes256gcm_is_available() != 0;
    if (m_available)
        qDebug() << "CryptoManager: AES-256-GCM available (AES-NI)";
    else
        qWarning() << "CryptoManager: AES-256-GCM NOT available — encryption will fail";
}

QByteArray CryptoManager::deriveKey(const QString &mnemonic,
                                     const QByteArray &salt) const
{
    if (salt.size() < static_cast<int>(crypto_pwhash_SALTBYTES))
        return {};

    SecureBuffer key(KEY_BYTES);
    SecureBuffer pw(mnemonic.toUtf8());

    int rc = crypto_pwhash(
        reinterpret_cast<unsigned char *>(key.data()), KEY_BYTES,
        pw.constData(), static_cast<unsigned long long>(pw.size()),
        reinterpret_cast<const unsigned char *>(salt.constData()),
        crypto_pwhash_OPSLIMIT_MODERATE,
        crypto_pwhash_MEMLIMIT_MODERATE,
        crypto_pwhash_ALG_ARGON2ID13);

    if (rc != 0)
        return {};

    return key.toByteArray();
}

QByteArray CryptoManager::encrypt(const QByteArray &plaintext,
                                   const QByteArray &key,
                                   QByteArray       &nonceOut) const
{
    nonceOut.resize(NONCE_BYTES);
    randombytes_buf(nonceOut.data(), NONCE_BYTES);

    QByteArray ciphertext(plaintext.size() + crypto_aead_aes256gcm_ABYTES, '\0');
    unsigned long long cipherLen = 0;

    int rc = crypto_aead_aes256gcm_encrypt(
        reinterpret_cast<unsigned char *>(ciphertext.data()), &cipherLen,
        reinterpret_cast<const unsigned char *>(plaintext.constData()), plaintext.size(),
        nullptr, 0, nullptr,
        reinterpret_cast<const unsigned char *>(nonceOut.constData()),
        reinterpret_cast<const unsigned char *>(key.constData()));

    if (rc != 0)
        return {};

    ciphertext.resize(static_cast<int>(cipherLen));
    return ciphertext;
}

QByteArray CryptoManager::decrypt(const QByteArray &ciphertext,
                                   const QByteArray &key,
                                   const QByteArray &nonce) const
{
    if (ciphertext.size() < static_cast<int>(crypto_aead_aes256gcm_ABYTES))
        return {};

    // Validate nonce length before passing to libsodium.
    if (nonce.size() != NONCE_BYTES) {
        qWarning() << "CryptoManager::decrypt: nonce size mismatch:"
                   << nonce.size() << "expected" << NONCE_BYTES;
        return {};
    }

    QByteArray plaintext(ciphertext.size() - crypto_aead_aes256gcm_ABYTES, '\0');
    unsigned long long plainLen = 0;

    int rc = crypto_aead_aes256gcm_decrypt(
        reinterpret_cast<unsigned char *>(plaintext.data()), &plainLen,
        nullptr,
        reinterpret_cast<const unsigned char *>(ciphertext.constData()), ciphertext.size(),
        nullptr, 0,
        reinterpret_cast<const unsigned char *>(nonce.constData()),
        reinterpret_cast<const unsigned char *>(key.constData()));

    if (rc != 0)
        return {};

    plaintext.resize(static_cast<int>(plainLen));
    return plaintext;
}

QByteArray CryptoManager::deriveKeyFromPin(const QString &pin,
                                            const QByteArray &salt) const
{
    if (salt.size() < static_cast<int>(crypto_pwhash_SALTBYTES))
        return {};

    SecureBuffer key(KEY_BYTES);
    SecureBuffer pw(pin.toUtf8());

    int rc = crypto_pwhash(
        reinterpret_cast<unsigned char *>(key.data()), KEY_BYTES,
        pw.constData(), static_cast<unsigned long long>(pw.size()),
        reinterpret_cast<const unsigned char *>(salt.constData()),
        crypto_pwhash_OPSLIMIT_MODERATE,
        crypto_pwhash_MEMLIMIT_MODERATE,
        crypto_pwhash_ALG_ARGON2ID13);

    if (rc != 0)
        return {};

    return key.toByteArray();
}

QByteArray CryptoManager::randomSalt()
{
    QByteArray salt(crypto_pwhash_SALTBYTES, '\0');
    randombytes_buf(salt.data(), salt.size());
    return salt;
}
