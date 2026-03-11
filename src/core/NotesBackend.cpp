#include "NotesBackend.h"

#include <QDebug>

NotesBackend::NotesBackend(QObject *parent)
    : QObject(parent)
{
    m_db.init();

    if (m_db.isInitialized())
        m_currentScreen = "unlock";
    else
        m_currentScreen = "import";
}

QString NotesBackend::currentScreen() const
{
    return m_currentScreen;
}

QString NotesBackend::errorMessage() const
{
    return m_errorMessage;
}

void NotesBackend::importMnemonic(const QString &mnemonic,
                                   const QString &pin,
                                   const QString &pinConfirm)
{
    if (pin != pinConfirm) {
        setError("PINs do not match.");
        return;
    }
    if (pin.length() < KeyManager::PIN_MIN_LENGTH) {
        setError(QString("PIN must be at least %1 digits.").arg(KeyManager::PIN_MIN_LENGTH));
        return;
    }
    if (!KeyManager::isValidMnemonic(mnemonic)) {
        setError("Invalid recovery phrase. Enter 12 or 24 words.");
        return;
    }

    // 1. Derive the master key from the mnemonic (never stored).
    const QByteArray masterKey = m_crypto.deriveKey(mnemonic);
    if (masterKey.isEmpty()) {
        setError("Key derivation failed.");
        return;
    }

    // 2. Derive a wrapping key from the PIN with a fresh random salt.
    const QByteArray pinSalt = CryptoManager::randomSalt();
    const QByteArray pinKey  = m_crypto.deriveKeyFromPin(pin, pinSalt);
    if (pinKey.isEmpty()) {
        setError("PIN key derivation failed.");
        return;
    }

    // 3. Encrypt the master key with the PIN-derived key.
    QByteArray wrapNonce;
    const QByteArray wrappedKey = m_crypto.encrypt(masterKey, pinKey, wrapNonce);
    if (wrappedKey.isEmpty()) {
        setError("Key wrapping failed.");
        return;
    }

    // 4. Persist the wrapped key so unlock() can restore the master key
    //    without the mnemonic.
    if (!m_db.saveWrappedKey(wrappedKey, wrapNonce, pinSalt)) {
        setError("Failed to save key.");
        return;
    }

    // 5. Hold the master key in memory for this session.
    m_keys.setMasterKey(masterKey);

    m_db.setInitialized();
    setError({});
    setScreen("note");
}

void NotesBackend::unlockWithPin(const QString &pin)
{
    if (pin.length() < KeyManager::PIN_MIN_LENGTH) {
        setError(QString("PIN must be at least %1 digits.").arg(KeyManager::PIN_MIN_LENGTH));
        return;
    }

    // 1. Load the wrapped master key stored during import.
    QByteArray wrappedKey, wrapNonce, pinSalt;
    if (!m_db.loadWrappedKey(wrappedKey, wrapNonce, pinSalt)) {
        setError("No account found. Please re-import your recovery phrase.");
        return;
    }

    // 2. Re-derive the PIN wrapping key using the stored salt.
    const QByteArray pinKey = m_crypto.deriveKeyFromPin(pin, pinSalt);
    if (pinKey.isEmpty()) {
        setError("Key derivation failed.");
        return;
    }

    // 3. Decrypt the master key. AES-GCM authentication tag verification
    //    happens here — wrong PIN produces an empty result.
    const QByteArray masterKey = m_crypto.decrypt(wrappedKey, pinKey, wrapNonce);
    if (masterKey.isEmpty()) {
        setError("Wrong PIN.");
        return;
    }

    // 4. Master key is back in memory; ready to decrypt notes.
    m_keys.setMasterKey(masterKey);
    setError({});
    setScreen("note");
}

void NotesBackend::saveNote(const QString &plaintext)
{
    if (!m_keys.isUnlocked()) {
        setError("Not unlocked.");
        return;
    }

    QByteArray nonce;
    const QByteArray ciphertext =
        m_crypto.encrypt(plaintext.toUtf8(), m_keys.masterKey(), nonce);

    if (ciphertext.isEmpty()) {
        setError("Encryption failed.");
        return;
    }

    m_db.saveNote(ciphertext, nonce);
}

QString NotesBackend::loadNote()
{
    if (!m_keys.isUnlocked())
        return {};

    QByteArray ciphertext, nonce;
    if (!m_db.loadNote(ciphertext, nonce))
        return {};

    const QByteArray plaintext =
        m_crypto.decrypt(ciphertext, m_keys.masterKey(), nonce);

    return QString::fromUtf8(plaintext);
}

void NotesBackend::resetAndWipe()
{
    m_keys.lock();
    m_db.wipe();
    m_db.init();
    setError({});
    setScreen("import");
}

void NotesBackend::setScreen(const QString &screen)
{
    if (m_currentScreen != screen) {
        m_currentScreen = screen;
        emit currentScreenChanged();
    }
}

void NotesBackend::setError(const QString &msg)
{
    if (m_errorMessage != msg) {
        m_errorMessage = msg;
        emit errorMessageChanged();
    }
}
