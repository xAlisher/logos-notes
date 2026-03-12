#include "NotesBackend.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static QString titleFromPlaintext(const QString &text)
{
    // First non-empty line, trimmed to 100 chars.
    const auto lines = text.split('\n');
    for (const auto &line : lines) {
        QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            return trimmed.left(100);
    }
    return {};
}

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

    // Phase 0 compat: always targets id=1.
    // Use the old upsert to ensure the row exists, then update with title + timestamp.
    m_db.saveNote(ciphertext, nonce);
    m_db.saveNote(1, ciphertext, nonce, titleFromPlaintext(plaintext));
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

// ── Phase 1: multi-note API ──────────────────────────────────────────────

QString NotesBackend::createNote()
{
    if (!m_keys.isUnlocked()) {
        setError("Not unlocked.");
        return {};
    }
    int id = m_db.createNote();
    if (id < 0) {
        setError("Failed to create note.");
        return {};
    }
    QJsonObject obj;
    obj["id"] = id;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString NotesBackend::loadNotes()
{
    if (!m_keys.isUnlocked()) {
        setError("Not unlocked.");
        return "[]";
    }
    const auto headers = m_db.loadNoteHeaders();
    QJsonArray arr;
    for (const auto &h : headers) {
        QJsonObject obj;
        obj["id"] = h.id;
        obj["title"] = h.title;
        obj["updatedAt"] = h.updatedAt;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString NotesBackend::loadNote(int id)
{
    if (!m_keys.isUnlocked()) {
        setError("Not unlocked.");
        return {};
    }
    QByteArray ciphertext, nonce;
    if (!m_db.loadNote(id, ciphertext, nonce))
        return {};
    // Empty ciphertext means newly created note with no content yet.
    if (ciphertext.isEmpty())
        return {};
    const QByteArray plaintext =
        m_crypto.decrypt(ciphertext, m_keys.masterKey(), nonce);
    return QString::fromUtf8(plaintext);
}

QString NotesBackend::saveNote(int id, const QString &plaintext)
{
    if (!m_keys.isUnlocked()) {
        setError("Not unlocked.");
        return {};
    }
    QByteArray nonce;
    const QByteArray ciphertext =
        m_crypto.encrypt(plaintext.toUtf8(), m_keys.masterKey(), nonce);
    if (ciphertext.isEmpty()) {
        setError("Encryption failed.");
        return {};
    }
    const QString title = titleFromPlaintext(plaintext);
    if (!m_db.saveNote(id, ciphertext, nonce, title)) {
        setError("Failed to save note.");
        return {};
    }
    return QStringLiteral("ok");
}

QString NotesBackend::deleteNote(int id)
{
    if (!m_keys.isUnlocked()) {
        setError("Not unlocked.");
        return {};
    }
    if (!m_db.deleteNote(id)) {
        setError("Failed to delete note.");
        return {};
    }
    return QStringLiteral("ok");
}

void NotesBackend::lock()
{
    m_keys.lock();
    setError({});
    setScreen("unlock");
}

bool NotesBackend::hasAccount() const
{
    return m_db.isInitialized();
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
