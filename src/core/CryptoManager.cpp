#include "CryptoManager.h"
#include "SecureBuffer.h"

#include <sodium.h>
#include <QDebug>

CryptoManager::CryptoManager()
    : m_cipher(AES256GCM) // default; overridden by setCipher() or detectBestCipher()
{
    if (sodium_init() < 0) {
        // -1 means failure; 1 means already initialised — both fine here.
    }
}

void CryptoManager::setCipher(Cipher c)
{
    m_cipher = c;
    qDebug() << "CryptoManager: cipher set to" << cipherToString(c);
}

CryptoManager::Cipher CryptoManager::detectBestCipher()
{
    // Ensure sodium is initialised before checking.
    sodium_init();
    if (crypto_aead_aes256gcm_is_available())
        return AES256GCM;
    return XCHACHA20POLY1305;
}

QString CryptoManager::cipherToString(Cipher c)
{
    return c == AES256GCM ? QStringLiteral("aes256gcm")
                          : QStringLiteral("xchacha20");
}

CryptoManager::Cipher CryptoManager::cipherFromString(const QString &s)
{
    if (s == QStringLiteral("xchacha20"))
        return XCHACHA20POLY1305;
    return AES256GCM; // default for legacy DBs (all existing data is AES-GCM)
}

int CryptoManager::nonceBytes() const
{
    return m_cipher == AES256GCM
        ? static_cast<int>(crypto_aead_aes256gcm_NPUBBYTES)               // 12
        : static_cast<int>(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES); // 24
}

int CryptoManager::aBytes() const
{
    return m_cipher == AES256GCM
        ? static_cast<int>(crypto_aead_aes256gcm_ABYTES)               // 16
        : static_cast<int>(crypto_aead_xchacha20poly1305_ietf_ABYTES); // 16
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
    const int nb = nonceBytes();
    nonceOut.resize(nb);
    randombytes_buf(nonceOut.data(), nb);

    QByteArray ciphertext(plaintext.size() + aBytes(), '\0');
    unsigned long long cipherLen = 0;
    int rc;

    if (m_cipher == AES256GCM) {
        rc = crypto_aead_aes256gcm_encrypt(
            reinterpret_cast<unsigned char *>(ciphertext.data()), &cipherLen,
            reinterpret_cast<const unsigned char *>(plaintext.constData()), plaintext.size(),
            nullptr, 0, nullptr,
            reinterpret_cast<const unsigned char *>(nonceOut.constData()),
            reinterpret_cast<const unsigned char *>(key.constData()));
    } else {
        rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
            reinterpret_cast<unsigned char *>(ciphertext.data()), &cipherLen,
            reinterpret_cast<const unsigned char *>(plaintext.constData()), plaintext.size(),
            nullptr, 0, nullptr,
            reinterpret_cast<const unsigned char *>(nonceOut.constData()),
            reinterpret_cast<const unsigned char *>(key.constData()));
    }

    if (rc != 0)
        return {};

    ciphertext.resize(static_cast<int>(cipherLen));
    return ciphertext;
}

QByteArray CryptoManager::decrypt(const QByteArray &ciphertext,
                                   const QByteArray &key,
                                   const QByteArray &nonce) const
{
    if (ciphertext.size() < aBytes())
        return {};

    // Validate nonce length before passing to libsodium.
    if (nonce.size() != nonceBytes()) {
        qWarning() << "CryptoManager::decrypt: nonce size mismatch:"
                   << nonce.size() << "expected" << nonceBytes();
        return {};
    }

    QByteArray plaintext(ciphertext.size() - aBytes(), '\0');
    unsigned long long plainLen = 0;
    int rc;

    if (m_cipher == AES256GCM) {
        rc = crypto_aead_aes256gcm_decrypt(
            reinterpret_cast<unsigned char *>(plaintext.data()), &plainLen,
            nullptr,
            reinterpret_cast<const unsigned char *>(ciphertext.constData()), ciphertext.size(),
            nullptr, 0,
            reinterpret_cast<const unsigned char *>(nonce.constData()),
            reinterpret_cast<const unsigned char *>(key.constData()));
    } else {
        rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char *>(plaintext.data()), &plainLen,
            nullptr,
            reinterpret_cast<const unsigned char *>(ciphertext.constData()), ciphertext.size(),
            nullptr, 0,
            reinterpret_cast<const unsigned char *>(nonce.constData()),
            reinterpret_cast<const unsigned char *>(key.constData()));
    }

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
