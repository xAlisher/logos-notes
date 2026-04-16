#include "NotesBackend.h"
#include "SecureBuffer.h"

#include <sodium.h>
#include <QCryptographicHash>
#include <QThread>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// Canonical BIP39 normalization: NFKD, simplified whitespace, lowercase.
// Must be used before any crypto operation on the mnemonic to ensure
// identical phrases always produce identical keys and fingerprints.
static QString normalizeMnemonic(const QString &mnemonic)
{
    return mnemonic.simplified()
                   .normalized(QString::NormalizationForm_KD)
                   .toLower();
}

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
    // Fail fast if AES-NI hardware is not available.
    if (!m_crypto.isAvailable()) {
        m_currentScreen = "import";
        m_errorMessage = "AES hardware acceleration not available on this CPU. "
                         "Immutable Notes requires AES-NI (available on all CPUs since 2010).";
        return;
    }

    m_db.init();

    // Restore brute-force counter from DB (survives app restarts).
    m_failedAttempts = m_db.loadMeta("pin_failed_attempts", "0").toInt();
    m_lockoutUntil = m_db.loadMeta("pin_lockout_until", "0").toLongLong();

    if (m_db.isInitialized())
        m_currentScreen = "unlock";
    else
        m_currentScreen = "import";

    // Debounce timer for auto-backup (only fires in keycard sessions).
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(30000);
    connect(&m_debounceTimer, &QTimer::timeout, this, &NotesBackend::doAutoBackup);

    // Restore last persisted storage status across restarts.
    m_storageStatus = m_db.loadMeta("storage_status");
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
                                   const QString &pinConfirm,
                                   const QString &backupPath)
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

    // Normalize mnemonic before any crypto use (NFKD, whitespace, lowercase).
    const QString normalized = normalizeMnemonic(mnemonic);

    // 1. Derive the master key from the mnemonic with a random salt (never stored).
    const QByteArray mnemonicSalt = CryptoManager::randomSalt();
    SecureBuffer masterKey(m_crypto.deriveKey(normalized, mnemonicSalt));
    if (masterKey.isEmpty()) {
        setError("Key derivation failed.");
        return;
    }

    // 2. Derive a wrapping key from the PIN with a fresh random salt.
    const QByteArray pinSalt = CryptoManager::randomSalt();
    SecureBuffer pinKey(m_crypto.deriveKeyFromPin(pin, pinSalt));
    if (pinKey.isEmpty()) {
        setError("PIN key derivation failed.");
        return;
    }

    // 3. Encrypt the master key with the PIN-derived key.
    QByteArray wrapNonce;
    const QByteArray wrappedKey = m_crypto.encrypt(masterKey.ref(), pinKey.ref(), wrapNonce);
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

    // 5. Persist the mnemonic KDF salt and account fingerprint.
    //    Both are critical for cross-device backup restore.
    if (!m_db.saveMetaBlob("mnemonic_kdf_salt", mnemonicSalt) ||
        !m_db.saveMeta("account_fingerprint", deriveFingerprint(normalized))) {
        // Rollback: metadata persistence failed — account is unusable for backup.
        m_keys.lock();
        m_db.wipe();
        m_db.init();
        setError("Failed to save account metadata. Please try again.");
        setScreen("import");
        return;
    }

    // 6. Hold the master key in memory for this session.
    m_keys.setMasterKey(masterKey.toByteArray());

    // 7. Restore backup if a path was provided (cross-device restore).
    if (!backupPath.isEmpty()) {
        QString result = importBackup(backupPath, normalized);
        QJsonObject parsed = QJsonDocument::fromJson(result.toUtf8()).object();
        if (!parsed.value("ok").toBool()) {
            // Rollback: wipe the just-created account so user can retry.
            m_keys.lock();
            m_db.wipe();
            m_db.init();
            setError(parsed.value("error").toString("Backup restore failed."));
            setScreen("import");
            return;
        }
        int restoredCount = parsed.value("imported").toInt();
        int failedCount = parsed.value("failed").toInt(0);
        qDebug() << "NotesBackend: restored" << restoredCount << "note(s),"
                 << failedCount << "failed";
        if (failedCount > 0) {
            setError(QString("Restored %1 note(s), %2 failed to restore.")
                         .arg(restoredCount).arg(failedCount));
        }
    }

    m_db.setInitialized();
    m_keySource = QStringLiteral("mnemonic");
    setError({});
    setScreen("note");
}

void NotesBackend::unlockWithPin(const QString &pin)
{
    // ── Brute-force protection (Issue #2) ──────────────────────────────
    // NOTE: lockout state is stored in the same DB as the wrapped key.
    // An offline attacker can reset the counters directly in SQLite.
    // This is a known limitation (Issue #10, see SECURITY_REVIEW.md).
    // The primary offline defense is Argon2id cost, not this counter.
    static constexpr int MAX_ATTEMPTS = 5;
    // Backoff schedule: 0, 0, 0, 0, 0, then 30s, 60s, 120s, 300s, 600s…
    static constexpr int BACKOFF_SECS[] = {30, 60, 120, 300, 600};
    static constexpr int BACKOFF_COUNT = sizeof(BACKOFF_SECS) / sizeof(BACKOFF_SECS[0]);

    if (m_lockoutUntil > 0) {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (now < m_lockoutUntil) {
            int remaining = static_cast<int>(m_lockoutUntil - now);
            setError(QString("Too many failed attempts. Try again in %1 seconds.")
                         .arg(remaining));
            return;
        }
        m_lockoutUntil = 0; // lockout expired
    }

    if (pin.length() < KeyManager::PIN_MIN_LENGTH) {
        setError(QString("PIN must be at least %1 characters.").arg(KeyManager::PIN_MIN_LENGTH));
        return;
    }

    // 1. Load the wrapped master key stored during import.
    QByteArray wrappedKey, wrapNonce, pinSalt;
    if (!m_db.loadWrappedKey(wrappedKey, wrapNonce, pinSalt)) {
        setError("No account found. Please re-import your recovery phrase.");
        return;
    }

    // 2. Re-derive the PIN wrapping key using the stored salt.
    SecureBuffer pinKey(m_crypto.deriveKeyFromPin(pin, pinSalt));
    if (pinKey.isEmpty()) {
        setError("Key derivation failed.");
        return;
    }

    // 3. Decrypt the master key. AES-GCM authentication tag verification
    //    happens here — wrong PIN produces an empty result.
    SecureBuffer masterKey(m_crypto.decrypt(wrappedKey, pinKey.ref(), wrapNonce));
    if (masterKey.isEmpty()) {
        ++m_failedAttempts;
        if (!m_db.saveMeta("pin_failed_attempts", QString::number(m_failedAttempts)))
            qWarning() << "NotesBackend: failed to persist PIN attempt counter";
        if (m_failedAttempts >= MAX_ATTEMPTS) {
            int idx = qMin(m_failedAttempts - MAX_ATTEMPTS, BACKOFF_COUNT - 1);
            m_lockoutUntil = QDateTime::currentSecsSinceEpoch() + BACKOFF_SECS[idx];
            if (!m_db.saveMeta("pin_lockout_until", QString::number(m_lockoutUntil)))
                qWarning() << "NotesBackend: failed to persist PIN lockout timestamp";
            setError(QString("Wrong PIN. Locked out for %1 seconds.").arg(BACKOFF_SECS[idx]));
        } else {
            int remaining = MAX_ATTEMPTS - m_failedAttempts;
            setError(QString("Wrong PIN. %1 attempt(s) remaining.").arg(remaining));
        }
        return;
    }

    // 4. Success — reset brute-force counter.
    m_failedAttempts = 0;
    m_lockoutUntil = 0;
    if (!m_db.saveMeta("pin_failed_attempts", "0"))
        qWarning() << "NotesBackend: failed to reset PIN attempt counter";
    if (!m_db.saveMeta("pin_lockout_until", "0"))
        qWarning() << "NotesBackend: failed to reset PIN lockout timestamp";

    // 5. Master key is back in memory; ready to decrypt notes.
    m_keys.setMasterKey(masterKey.toByteArray());

    // 6. Migrate any legacy plaintext titles to encrypted.
    migratePlaintextTitles();

    m_keySource = QStringLiteral("mnemonic");
    setError({});
    setScreen("note");
}

// ── Note CRUD ────────────────────────────────────────────────────────────

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

        // Decrypt title if encrypted; fall back to legacy plaintext.
        if (!h.titleCiphertext.isEmpty() && !h.titleNonce.isEmpty()) {
            QByteArray titlePt = m_crypto.decrypt(h.titleCiphertext,
                                                   m_keys.masterKey(),
                                                   h.titleNonce);
            obj["title"] = QString::fromUtf8(titlePt);
        } else {
            obj["title"] = h.titlePlaintext;
        }

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
    QByteArray titleNonce;
    const QByteArray titleCt =
        m_crypto.encrypt(title.toUtf8(), m_keys.masterKey(), titleNonce);
    if (!m_db.saveNote(id, ciphertext, nonce, titleCt, titleNonce)) {
        setError("Failed to save note.");
        return {};
    }

    // Arm the auto-backup debounce. Only in keycard sessions — mnemonic
    // sessions get no auto-backup (issue #72). start() restarts the timer
    // if already running, coalescing rapid saves into a single upload.
    if (m_keySource == QLatin1String("keycard"))
        m_debounceTimer.start();

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
    m_debounceTimer.stop();
    m_keySource.clear();
    ++m_sessionGeneration;   // fence any in-flight upload callbacks
    m_keys.lock();
    setError({});
    setScreen("unlock");
}

QString NotesBackend::getAccountFingerprint() const
{
    // Stored fingerprint works on unlock screen (no key needed).
    QString stored = m_db.loadMeta("account_fingerprint");
    if (!stored.isEmpty())
        return stored;
    return {};
}

QString NotesBackend::deriveFingerprint(const QString &mnemonic)
{
    // Deterministic: Ed25519 public key derived from SHA-256(normalized mnemonic).
    // Same mnemonic → same public key, always, on any device.
    // Normalization ensures spacing/casing/unicode differences don't matter.
    const QString normalized = normalizeMnemonic(mnemonic);
    QByteArray seed = QCryptographicHash::hash(normalized.toUtf8(),
                                                QCryptographicHash::Sha256);
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_seed_keypair(pk, sk,
        reinterpret_cast<const unsigned char *>(seed.constData()));
    sodium_memzero(sk, sizeof(sk));
    sodium_memzero(seed.data(), seed.size());
    return QByteArray(reinterpret_cast<const char *>(pk),
                      crypto_sign_PUBLICKEYBYTES).toHex().toUpper();
}

bool NotesBackend::hasAccount() const
{
    return m_db.isInitialized();
}

QString NotesBackend::getKeySource() const
{
    return m_db.loadMeta("key_source", "mnemonic");
}

QString NotesBackend::exportBackup(const QString &filePath)
{
    if (!m_keys.isUnlocked())
        return QStringLiteral("{\"error\":\"Not unlocked\"}");

    // Collect all notes as plaintext JSON array.
    const auto headers = m_db.loadNoteHeaders();
    QJsonArray notesArr;
    for (const auto &h : headers) {
        QByteArray ct, nonce;
        if (!m_db.loadNote(h.id, ct, nonce))
            continue;
        QString content;
        if (!ct.isEmpty())
            content = QString::fromUtf8(m_crypto.decrypt(ct, m_keys.masterKey(), nonce));

        // Decrypt title.
        QString title;
        if (!h.titleCiphertext.isEmpty() && !h.titleNonce.isEmpty())
            title = QString::fromUtf8(m_crypto.decrypt(h.titleCiphertext,
                                                        m_keys.masterKey(), h.titleNonce));

        QJsonObject noteObj;
        noteObj["title"] = title;
        noteObj["content"] = content;
        noteObj["updatedAt"] = h.updatedAt;
        notesArr.append(noteObj);
    }

    // Encrypt the JSON blob.
    QByteArray plaintext = QJsonDocument(notesArr).toJson(QJsonDocument::Compact);
    QByteArray nonce;
    QByteArray ciphertext = m_crypto.encrypt(plaintext, m_keys.masterKey(), nonce);
    if (ciphertext.isEmpty())
        return QStringLiteral("{\"error\":\"Encryption failed\"}");

    // Build backup file: salt + nonce + ciphertext so any device with the
    // same mnemonic can re-derive the key and decrypt.
    QByteArray salt = m_db.loadMetaBlob("mnemonic_kdf_salt");
    QJsonObject backup;
    backup["version"] = 1;
    backup["salt"] = QString::fromLatin1(salt.toBase64());
    backup["nonce"] = QString::fromLatin1(nonce.toBase64());
    backup["ciphertext"] = QString::fromLatin1(ciphertext.toBase64());
    backup["noteCount"] = notesArr.size();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return QStringLiteral("{\"error\":\"Cannot write file\"}");
    }
    file.write(QJsonDocument(backup).toJson(QJsonDocument::Compact));
    file.close();

    QJsonObject result;
    result["ok"] = true;
    result["noteCount"] = notesArr.size();
    result["path"] = filePath;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString NotesBackend::backupsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
           + "/.local/share/logos-notes/backups";
}

QString NotesBackend::exportBackupAuto()
{
    if (!m_keys.isUnlocked())
        return QStringLiteral("{\"error\":\"Not unlocked\"}");

    QString dir = backupsDir();
    QDir().mkpath(dir);

    QString fp = getAccountFingerprint().left(16);
    QDateTime now = QDateTime::currentDateTime();
    QString filename = fp + "_" + now.toString("yyyy-MM-dd_HHmm") + ".imnotes";
    QString path = dir + "/" + filename;

    return exportBackup(path);
}

QString NotesBackend::listBackups() const
{
    QString dirPath = backupsDir();
    QDir dir(dirPath);
    QStringList files = dir.entryList({"*.imnotes"}, QDir::Files, QDir::Time);
    QJsonArray arr;
    for (const QString &f : files) {
        QJsonObject obj;
        obj["name"] = f;
        obj["path"] = dirPath + "/" + f;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString NotesBackend::importBackup(const QString &filePath,
                                    const QString &mnemonic)
{
    if (!m_keys.isUnlocked())
        return QStringLiteral("{\"error\":\"Not unlocked\"}");

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("{\"error\":\"Cannot read file\"}");

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return QStringLiteral("{\"error\":\"Invalid backup format\"}");

    QJsonObject backup = doc.object();
    if (backup["version"].toInt() != 1)
        return QStringLiteral("{\"error\":\"Unsupported backup version\"}");

    QByteArray backupSalt = QByteArray::fromBase64(backup["salt"].toString().toLatin1());
    QByteArray nonce = QByteArray::fromBase64(backup["nonce"].toString().toLatin1());
    QByteArray ciphertext = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());

    // Try decrypting with current master key first (same-device restore).
    QByteArray plaintext = m_crypto.decrypt(ciphertext, m_keys.masterKey(), nonce);

    // If that fails, re-derive using backup's salt + normalized mnemonic.
    if (plaintext.isEmpty() && !mnemonic.isEmpty()) {
        SecureBuffer backupKey(m_crypto.deriveKey(normalizeMnemonic(mnemonic), backupSalt));
        if (!backupKey.isEmpty())
            plaintext = m_crypto.decrypt(ciphertext, backupKey.ref(), nonce);
    }

    if (plaintext.isEmpty())
        return QStringLiteral("{\"error\":\"Cannot decrypt backup. "
                              "Wrong recovery phrase or corrupted file.\"}");

    QJsonArray notesArr = QJsonDocument::fromJson(plaintext).array();
    int imported = 0;
    int failed = 0;
    for (const auto &val : notesArr) {
        QJsonObject noteObj = val.toObject();
        QString content = noteObj["content"].toString();
        QString title = noteObj["title"].toString();

        int id = m_db.createNote();
        if (id < 0) { ++failed; continue; }

        // Encrypt content.
        QByteArray contentNonce;
        QByteArray contentCt = m_crypto.encrypt(content.toUtf8(),
                                                 m_keys.masterKey(), contentNonce);
        if (contentCt.isEmpty()) { ++failed; m_db.deleteNote(id); continue; }

        // Encrypt title.
        QByteArray titleNonce;
        QByteArray titleCt = m_crypto.encrypt(title.toUtf8(),
                                               m_keys.masterKey(), titleNonce);
        if (titleCt.isEmpty()) { ++failed; m_db.deleteNote(id); continue; }

        if (!m_db.saveNote(id, contentCt, contentNonce, titleCt, titleNonce)) {
            ++failed;
            m_db.deleteNote(id);
            continue;
        }

        ++imported;
    }

    QJsonObject result;
    result["ok"] = (imported > 0 || failed == 0);
    result["imported"] = imported;
    if (failed > 0)
        result["failed"] = failed;
    if (imported == 0 && failed > 0)
        result["error"] = QStringLiteral("All notes failed to restore.");
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void NotesBackend::resetAndWipe()
{
    m_debounceTimer.stop();
    m_keySource.clear();
    m_storageStatus.clear();
    ++m_sessionGeneration;   // fence any in-flight upload callbacks
    m_keys.lock();
    m_db.wipe();
    m_db.init();
    setError({});
    setScreen("import");
}

void NotesBackend::migratePlaintextTitles()
{
    if (!m_keys.isUnlocked())
        return;

    const auto headers = m_db.loadNoteHeaders();
    int migrated = 0;
    for (const auto &h : headers) {
        // Skip notes that already have encrypted titles.
        if (!h.titleCiphertext.isEmpty())
            continue;
        // Skip notes with no legacy plaintext title.
        if (h.titlePlaintext.isEmpty())
            continue;

        // Encrypt the legacy plaintext title.
        QByteArray titleNonce;
        QByteArray titleCt = m_crypto.encrypt(h.titlePlaintext.toUtf8(),
                                               m_keys.masterKey(), titleNonce);
        if (titleCt.isEmpty())
            continue;

        // Load the note's body ciphertext/nonce to pass through unchanged.
        QByteArray bodyCt, bodyNonce;
        if (!m_db.loadNote(h.id, bodyCt, bodyNonce))
            continue;

        m_db.saveNote(h.id, bodyCt, bodyNonce, titleCt, titleNonce);
        ++migrated;
    }
    if (migrated > 0)
        qDebug() << "NotesBackend: migrated" << migrated << "plaintext title(s) to encrypted";
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

// ── Keycard ──────────────────────────────────────────────────────────────────

// Derive a deterministic Ed25519 fingerprint from the master key.
static QString deriveFingerprintFromKey(const QByteArray &masterKey)
{
    unsigned char ed_pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char ed_sk[crypto_sign_SECRETKEYBYTES];
    unsigned char seed[crypto_sign_SEEDBYTES];

    QByteArray fpHash = QCryptographicHash::hash(masterKey, QCryptographicHash::Sha256);
    memcpy(seed, fpHash.constData(), crypto_sign_SEEDBYTES);
    crypto_sign_seed_keypair(ed_pk, ed_sk, seed);
    sodium_memzero(seed, sizeof(seed));
    sodium_memzero(ed_sk, sizeof(ed_sk));
    sodium_memzero(fpHash.data(), fpHash.size());

    return QByteArray(reinterpret_cast<const char*>(ed_pk),
                      crypto_sign_PUBLICKEYBYTES).toHex().left(16);
}

// ── Keycard Module Integration ───────────────────────────────────────────────
// Receive pre-derived key from keycard-basecamp module (replaces internal KeycardBridge)

void NotesBackend::importWithKeycardKey(const QString &hexKey,
                                         const QString &backupPath)
{
    // Convert hex key to bytes
    QByteArray keyBytes = QByteArray::fromHex(hexKey.toUtf8());
    if (keyBytes.isEmpty() || keyBytes.size() < 32) {
        setError("Invalid key from keycard module");
        return;
    }

    // Use first 32 bytes as AES-256 master key
    SecureBuffer masterKey(keyBytes.left(32));
    sodium_memzero(keyBytes.data(), keyBytes.size());

    // Store key source metadata
    m_db.saveMeta("key_source", "keycard");
    m_db.saveMeta("account_fingerprint", deriveFingerprintFromKey(masterKey.ref()));

    // Hold master key in memory
    m_keys.setMasterKey(masterKey.toByteArray());

    // Restore backup if provided
    if (!backupPath.isEmpty()) {
        QString result = importBackup(backupPath);
        QJsonObject parsed = QJsonDocument::fromJson(result.toUtf8()).object();
        if (!parsed.value("ok").toBool()) {
            m_keys.lock();
            m_db.wipe();
            m_db.init();
            setError(parsed.value("error").toString("Backup restore failed."));
            setScreen("import");
            return;
        }
        int failedCount = parsed.value("failed").toInt(0);
        if (failedCount > 0) {
            int restoredCount = parsed.value("imported").toInt();
            setError(QString("Restored %1 note(s), %2 failed to restore.").arg(restoredCount).arg(failedCount));
        }
    }

    m_db.setInitialized();
    m_keySource = QStringLiteral("keycard");
    setScreen("note");
}

void NotesBackend::unlockWithKeycardKey(const QString &hexKey)
{
    // Convert hex key to bytes
    QByteArray keyBytes = QByteArray::fromHex(hexKey.toUtf8());
    if (keyBytes.isEmpty() || keyBytes.size() < 32) {
        setError("Invalid key from keycard module");
        return;
    }

    SecureBuffer masterKey(keyBytes.left(32));
    sodium_memzero(keyBytes.data(), keyBytes.size());

    // Verify fingerprint matches stored account
    QString storedFp = m_db.loadMeta("account_fingerprint");
    if (!storedFp.isEmpty()) {
        QString currentFp = deriveFingerprintFromKey(masterKey.ref());
        if (currentFp != storedFp) {
            setError("Key mismatch — wrong card or domain");
            return;
        }
    }

    // Hold in memory and load notes
    m_keys.setMasterKey(masterKey.toByteArray());
    migratePlaintextTitles();
    m_keySource = QStringLiteral("keycard");
    setScreen("note");
}

// ── Storage auto-backup (issue #72) ─────────────────────────────────────────

void NotesBackend::setStorageClient(std::unique_ptr<StorageClient> client)
{
    m_storage = std::move(client);
}

QString NotesBackend::getBackupCid() const
{
    QString cid = m_db.loadMeta("backup_cid");
    if (cid.isEmpty())
        return QStringLiteral("{}");
    QJsonObject obj;
    obj["cid"]       = cid;
    obj["timestamp"] = m_db.loadMeta("backup_timestamp");
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString NotesBackend::getStorageStatus() const
{
    if (m_keySource != QLatin1String("keycard"))
        return QStringLiteral("disabled");
    if (m_storageStatus.isEmpty())
        return (m_storage && m_storage->isAvailable())
               ? QStringLiteral("available")
               : QStringLiteral("unavailable");
    return m_storageStatus;
}

QString NotesBackend::triggerBackup()
{
    if (m_keySource != QLatin1String("keycard"))
        return QStringLiteral("{\"error\":\"Auto-backup requires a Keycard session\"}");
    if (m_storageStatus == QLatin1String("uploading"))
        return QStringLiteral("{\"error\":\"Upload already in progress\"}");
    m_debounceTimer.stop();
    const QString startError = doAutoBackup();
    if (!startError.isEmpty()) {
        // No upload started — surface the reason as an error.
        QString safe = startError;
        safe.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QStringLiteral("{\"error\":\"") + safe + QStringLiteral("\"}");
    }
    return QStringLiteral("{\"success\":true}");
}

QString NotesBackend::doAutoBackup()
{
    if (!m_storage || !m_storage->isAvailable()) {
        m_storageStatus = QStringLiteral("unavailable");
        return QStringLiteral("storage_module not available");
    }

    // Write backup file locally first.
    QString exportResult = exportBackupAuto();
    QJsonObject exportObj = QJsonDocument::fromJson(exportResult.toUtf8()).object();
    if (!exportObj.value("ok").toBool()) {
        qWarning() << "NotesBackend::doAutoBackup: exportBackupAuto failed:" << exportResult;
        m_storageStatus = QStringLiteral("failed");
        if (!m_db.saveMeta("storage_status", m_storageStatus))
            qWarning() << "NotesBackend::doAutoBackup: failed to persist storage_status after export failure";
        return QStringLiteral("Export failed");
    }

    const QString filePath = exportObj.value("path").toString();
    m_storageStatus = QStringLiteral("uploading");

    // Capture generation so the callback can detect session change or wipe.
    const int myGeneration = m_sessionGeneration;

    m_storage->uploadFile(filePath, [this, myGeneration](const QString& cid, const QString& error) {
        // Discard callback if session changed (lock, wipe, or account switch).
        if (m_sessionGeneration != myGeneration || m_keySource != QLatin1String("keycard"))
            return;

        if (error.isEmpty() && !cid.isEmpty()) {
            const bool cidOk = m_db.saveMeta("backup_cid", cid);
            const bool tsOk  = m_db.saveMeta(
                                   "backup_timestamp",
                                   QString::number(QDateTime::currentSecsSinceEpoch()));
            if (cidOk && tsOk) {
                m_storageStatus = QStringLiteral("synced");
            } else {
                qWarning() << "NotesBackend::doAutoBackup: metadata write failed after upload";
                m_storageStatus = QStringLiteral("failed");
            }
        } else {
            qWarning() << "NotesBackend::doAutoBackup: upload failed:" << error;
            m_storageStatus = QStringLiteral("failed");
        }
        if (!m_db.saveMeta("storage_status", m_storageStatus))
            qWarning() << "NotesBackend::doAutoBackup: failed to persist storage_status" << m_storageStatus;
    });

    return {};  // upload started
}
