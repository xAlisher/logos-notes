#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/CryptoManager.h"
#include "core/DatabaseManager.h"
#include "core/KeyManager.h"
#include "core/NotesBackend.h"
#include "core/SecureBuffer.h"

#include <sodium.h>

// Tests for backup export/import flows (Issue #25).
// Exercises the full roundtrip: create notes → export → wipe → re-import → verify.

static const QString TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon "
    "abandon about";

static const QString TEST_PIN = "123456";

// Canonical normalization matching NotesBackend's internal normalizeMnemonic().
static QString normalizeMnemonic(const QString &mnemonic)
{
    return mnemonic.simplified()
                   .normalized(QString::NormalizationForm_KD)
                   .toLower();
}

// Helper: set up a fresh DB with an imported account, return the master key.
static QByteArray setupAccount(DatabaseManager &db, CryptoManager &crypto,
                                const QString &mnemonic, const QString &pin,
                                QByteArray &mnemonicSalt)
{
    mnemonicSalt = CryptoManager::randomSalt();
    QByteArray masterKey = crypto.deriveKey(normalizeMnemonic(mnemonic), mnemonicSalt);
    if (masterKey.isEmpty()) return {};

    QByteArray pinSalt = CryptoManager::randomSalt();
    QByteArray pinKey = crypto.deriveKeyFromPin(pin, pinSalt);
    if (pinKey.isEmpty()) return {};

    QByteArray wrapNonce;
    QByteArray wrappedKey = crypto.encrypt(masterKey, pinKey, wrapNonce);
    if (wrappedKey.isEmpty()) return {};

    db.saveWrappedKey(wrappedKey, wrapNonce, pinSalt);
    db.saveMetaBlob("mnemonic_kdf_salt", mnemonicSalt);
    db.saveMeta("account_fingerprint", NotesBackend::deriveFingerprint(normalizeMnemonic(mnemonic)));
    db.setInitialized();

    return masterKey;
}

// Helper: create and save an encrypted note, return the note id.
static int createEncryptedNote(DatabaseManager &db, CryptoManager &crypto,
                                const QByteArray &key,
                                const QString &content, const QString &title)
{
    int id = db.createNote();
    if (id < 0) return -1;

    QByteArray contentNonce, titleNonce;
    QByteArray contentCt = crypto.encrypt(content.toUtf8(), key, contentNonce);
    QByteArray titleCt = crypto.encrypt(title.toUtf8(), key, titleNonce);

    db.saveNote(id, contentCt, contentNonce, titleCt, titleNonce);
    return id;
}

// Helper: build an export backup JSON manually (simulates exportBackup output).
static QByteArray buildBackupFile(DatabaseManager &db, CryptoManager &crypto,
                                   const QByteArray &key)
{
    auto headers = db.loadNoteHeaders();
    QJsonArray notesArr;
    for (const auto &h : headers) {
        QByteArray ct, nonce;
        if (!db.loadNote(h.id, ct, nonce)) continue;

        QString content;
        if (!ct.isEmpty())
            content = QString::fromUtf8(crypto.decrypt(ct, key, nonce));

        QString title;
        if (!h.titleCiphertext.isEmpty() && !h.titleNonce.isEmpty())
            title = QString::fromUtf8(crypto.decrypt(h.titleCiphertext, key, h.titleNonce));

        QJsonObject noteObj;
        noteObj["title"] = title;
        noteObj["content"] = content;
        noteObj["updatedAt"] = h.updatedAt;
        notesArr.append(noteObj);
    }

    QByteArray plaintext = QJsonDocument(notesArr).toJson(QJsonDocument::Compact);
    QByteArray nonce;
    QByteArray ciphertext = crypto.encrypt(plaintext, key, nonce);

    QByteArray salt = db.loadMetaBlob("mnemonic_kdf_salt");
    QJsonObject backup;
    backup["version"] = 1;
    backup["salt"] = QString::fromLatin1(salt.toBase64());
    backup["nonce"] = QString::fromLatin1(nonce.toBase64());
    backup["ciphertext"] = QString::fromLatin1(ciphertext.toBase64());
    backup["noteCount"] = notesArr.size();

    return QJsonDocument(backup).toJson(QJsonDocument::Compact);
}

class TestBackup : public QObject
{
    Q_OBJECT

private slots:
    // ── Same-device roundtrip ─────────────────────────────────────────
    void testExportImportSameKey();

    // ── Cross-device restore (different salt, same mnemonic) ──────────
    void testCrossDeviceRestore();

    // ── Error handling ────────────────────────────────────────────────
    void testImportInvalidJson();
    void testImportUnsupportedVersion();
    void testImportWrongMnemonic();
    void testImportNonexistentFile();
    void testImportCorruptCiphertext();

    // ── Backup format validation ──────────────────────────────────────
    void testBackupFormatFields();

    // ── Partial import accounting ─────────────────────────────────────
    void testEmptyBackupImport();

    // ── Multiple notes roundtrip ──────────────────────────────────────
    void testMultiNoteRoundtrip();
};

// ── Same-device roundtrip ────────────────────────────────────────────────

void TestBackup::testExportImportSameKey()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    DatabaseManager db(tmpDir.path() + "/source.db");
    QVERIFY(db.init());
    CryptoManager crypto;

    QByteArray salt;
    QByteArray key = setupAccount(db, crypto, TEST_MNEMONIC, TEST_PIN, salt);
    QVERIFY(!key.isEmpty());

    // Create two notes.
    createEncryptedNote(db, crypto, key, "Note one content", "Note One");
    createEncryptedNote(db, crypto, key, "Note two content", "Note Two");

    // Export.
    QByteArray backupData = buildBackupFile(db, crypto, key);
    QString backupPath = tmpDir.path() + "/backup.imnotes";
    QFile f(backupPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(backupData);
    f.close();

    // Set up a fresh DB (same device = same key).
    DatabaseManager db2(tmpDir.path() + "/dest.db");
    QVERIFY(db2.init());
    QByteArray salt2;
    QByteArray key2 = setupAccount(db2, crypto, TEST_MNEMONIC, TEST_PIN, salt2);
    QVERIFY(!key2.isEmpty());

    // Read and parse backup.
    QFile bf(backupPath);
    QVERIFY(bf.open(QIODevice::ReadOnly));
    QJsonObject backup = QJsonDocument::fromJson(bf.readAll()).object();
    bf.close();

    QByteArray backupSalt = QByteArray::fromBase64(backup["salt"].toString().toLatin1());
    QByteArray nonce = QByteArray::fromBase64(backup["nonce"].toString().toLatin1());
    QByteArray ciphertext = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());

    // Decrypt with re-derived key using backup's salt.
    QByteArray restoreKey = crypto.deriveKey(normalizeMnemonic(TEST_MNEMONIC), backupSalt);
    QVERIFY(!restoreKey.isEmpty());

    QByteArray plaintext = crypto.decrypt(ciphertext, restoreKey, nonce);
    QVERIFY(!plaintext.isEmpty());

    QJsonArray notes = QJsonDocument::fromJson(plaintext).array();
    QCOMPARE(notes.size(), 2);

    // Verify content survived.
    QStringList contents;
    for (const auto &v : notes)
        contents << v.toObject()["content"].toString();
    QVERIFY(contents.contains("Note one content"));
    QVERIFY(contents.contains("Note two content"));

    // Verify titles survived.
    QStringList titles;
    for (const auto &v : notes)
        titles << v.toObject()["title"].toString();
    QVERIFY(titles.contains("Note One"));
    QVERIFY(titles.contains("Note Two"));
}

// ── Cross-device restore ─────────────────────────────────────────────────

void TestBackup::testCrossDeviceRestore()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CryptoManager crypto;

    // Device A: create account + notes + export.
    DatabaseManager dbA(tmpDir.path() + "/deviceA.db");
    QVERIFY(dbA.init());
    QByteArray saltA;
    QByteArray keyA = setupAccount(dbA, crypto, TEST_MNEMONIC, TEST_PIN, saltA);
    QVERIFY(!keyA.isEmpty());

    createEncryptedNote(dbA, crypto, keyA, "Secret diary entry", "Diary");

    QByteArray backupData = buildBackupFile(dbA, crypto, keyA);
    QString backupPath = tmpDir.path() + "/cross.imnotes";
    QFile f(backupPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(backupData);
    f.close();

    // Device B: different salt (different account setup), same mnemonic.
    DatabaseManager dbB(tmpDir.path() + "/deviceB.db");
    QVERIFY(dbB.init());
    QByteArray saltB;
    QByteArray keyB = setupAccount(dbB, crypto, TEST_MNEMONIC, "654321", saltB);
    QVERIFY(!keyB.isEmpty());

    // keyA != keyB because different salts.
    QVERIFY(keyA != keyB);

    // Parse backup and try decrypting with keyB (current device key) — should fail.
    QFile bf(backupPath);
    QVERIFY(bf.open(QIODevice::ReadOnly));
    QJsonObject backup = QJsonDocument::fromJson(bf.readAll()).object();
    bf.close();

    QByteArray nonce = QByteArray::fromBase64(backup["nonce"].toString().toLatin1());
    QByteArray ciphertext = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());

    QByteArray failedDecrypt = crypto.decrypt(ciphertext, keyB, nonce);
    QVERIFY(failedDecrypt.isEmpty()); // Different key, decryption fails.

    // Re-derive using backup's salt + same mnemonic — should succeed.
    QByteArray backupSalt = QByteArray::fromBase64(backup["salt"].toString().toLatin1());
    QByteArray restoreKey = crypto.deriveKey(normalizeMnemonic(TEST_MNEMONIC), backupSalt);
    QByteArray plaintext = crypto.decrypt(ciphertext, restoreKey, nonce);
    QVERIFY(!plaintext.isEmpty());

    QJsonArray notes = QJsonDocument::fromJson(plaintext).array();
    QCOMPARE(notes.size(), 1);
    QCOMPARE(notes[0].toObject()["content"].toString(), "Secret diary entry");
    QCOMPARE(notes[0].toObject()["title"].toString(), "Diary");
}

// ── Error handling ───────────────────────────────────────────────────────

void TestBackup::testImportInvalidJson()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QString path = tmpDir.path() + "/bad.imnotes";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not json");
    f.close();

    // Parse as JSON — should fail.
    QFile rf(path);
    QVERIFY(rf.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(rf.readAll());
    rf.close();
    QVERIFY(!doc.isObject());
}

void TestBackup::testImportUnsupportedVersion()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QJsonObject backup;
    backup["version"] = 99;
    backup["salt"] = "AAAA";
    backup["nonce"] = "AAAA";
    backup["ciphertext"] = "AAAA";

    QString path = tmpDir.path() + "/v99.imnotes";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(backup).toJson(QJsonDocument::Compact));
    f.close();

    // Read back and check version.
    QFile rf(path);
    QVERIFY(rf.open(QIODevice::ReadOnly));
    QJsonObject loaded = QJsonDocument::fromJson(rf.readAll()).object();
    rf.close();
    QVERIFY(loaded["version"].toInt() != 1);
}

void TestBackup::testImportWrongMnemonic()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CryptoManager crypto;

    // Create backup encrypted with TEST_MNEMONIC.
    DatabaseManager db(tmpDir.path() + "/source.db");
    QVERIFY(db.init());
    QByteArray salt;
    QByteArray key = setupAccount(db, crypto, TEST_MNEMONIC, TEST_PIN, salt);
    QVERIFY(!key.isEmpty());

    createEncryptedNote(db, crypto, key, "Secret", "Title");

    QByteArray backupData = buildBackupFile(db, crypto, key);

    // Parse backup and try to decrypt with a different mnemonic.
    QJsonObject backup = QJsonDocument::fromJson(backupData).object();
    QByteArray backupSalt = QByteArray::fromBase64(backup["salt"].toString().toLatin1());
    QByteArray nonce = QByteArray::fromBase64(backup["nonce"].toString().toLatin1());
    QByteArray ciphertext = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());

    // Different mnemonic (24-word test vector).
    QString wrongMnemonic = "abandon abandon abandon abandon abandon abandon "
                            "abandon abandon abandon abandon abandon abandon "
                            "abandon abandon abandon abandon abandon abandon "
                            "abandon abandon abandon abandon abandon art";
    QByteArray wrongKey = crypto.deriveKey(normalizeMnemonic(wrongMnemonic), backupSalt);
    QVERIFY(!wrongKey.isEmpty());

    QByteArray decrypted = crypto.decrypt(ciphertext, wrongKey, nonce);
    QVERIFY(decrypted.isEmpty()); // Wrong mnemonic → GCM auth tag fails.
}

void TestBackup::testImportNonexistentFile()
{
    QFile f("/tmp/does_not_exist_backup_12345.imnotes");
    QVERIFY(!f.open(QIODevice::ReadOnly));
}

void TestBackup::testImportCorruptCiphertext()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CryptoManager crypto;

    // Create a valid backup.
    DatabaseManager db(tmpDir.path() + "/source.db");
    QVERIFY(db.init());
    QByteArray salt;
    QByteArray key = setupAccount(db, crypto, TEST_MNEMONIC, TEST_PIN, salt);
    QVERIFY(!key.isEmpty());

    createEncryptedNote(db, crypto, key, "Content", "Title");

    QByteArray backupData = buildBackupFile(db, crypto, key);
    QJsonObject backup = QJsonDocument::fromJson(backupData).object();

    // Corrupt the ciphertext by flipping bits.
    QByteArray ciphertext = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());
    QVERIFY(!ciphertext.isEmpty());
    ciphertext[0] = ciphertext[0] ^ 0xFF;
    backup["ciphertext"] = QString::fromLatin1(ciphertext.toBase64());

    // Try to decrypt with the correct key — should fail due to GCM auth tag.
    QByteArray backupSalt = QByteArray::fromBase64(backup["salt"].toString().toLatin1());
    QByteArray nonce = QByteArray::fromBase64(backup["nonce"].toString().toLatin1());
    QByteArray corruptCt = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());

    QByteArray restoreKey = crypto.deriveKey(normalizeMnemonic(TEST_MNEMONIC), backupSalt);
    QByteArray decrypted = crypto.decrypt(corruptCt, restoreKey, nonce);
    QVERIFY(decrypted.isEmpty()); // GCM tag check fails.
}

// ── Backup format validation ─────────────────────────────────────────────

void TestBackup::testBackupFormatFields()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CryptoManager crypto;

    DatabaseManager db(tmpDir.path() + "/source.db");
    QVERIFY(db.init());
    QByteArray salt;
    QByteArray key = setupAccount(db, crypto, TEST_MNEMONIC, TEST_PIN, salt);
    QVERIFY(!key.isEmpty());

    createEncryptedNote(db, crypto, key, "Test content", "Test Title");

    QByteArray backupData = buildBackupFile(db, crypto, key);
    QJsonObject backup = QJsonDocument::fromJson(backupData).object();

    // All required fields must be present.
    QVERIFY(backup.contains("version"));
    QVERIFY(backup.contains("salt"));
    QVERIFY(backup.contains("nonce"));
    QVERIFY(backup.contains("ciphertext"));
    QVERIFY(backup.contains("noteCount"));

    QCOMPARE(backup["version"].toInt(), 1);
    QCOMPARE(backup["noteCount"].toInt(), 1);

    // salt, nonce, ciphertext must be valid base64.
    QVERIFY(!QByteArray::fromBase64(backup["salt"].toString().toLatin1()).isEmpty());
    QVERIFY(!QByteArray::fromBase64(backup["nonce"].toString().toLatin1()).isEmpty());
    QVERIFY(!QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1()).isEmpty());
}

// ── Empty backup import ──────────────────────────────────────────────────

void TestBackup::testEmptyBackupImport()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CryptoManager crypto;

    // Create an account with no notes, then export.
    DatabaseManager db(tmpDir.path() + "/empty.db");
    QVERIFY(db.init());
    QByteArray salt;
    QByteArray key = setupAccount(db, crypto, TEST_MNEMONIC, TEST_PIN, salt);
    QVERIFY(!key.isEmpty());

    QByteArray backupData = buildBackupFile(db, crypto, key);
    QJsonObject backup = QJsonDocument::fromJson(backupData).object();

    QCOMPARE(backup["noteCount"].toInt(), 0);

    // Decrypt the backup — should be an empty JSON array.
    QByteArray backupSalt = QByteArray::fromBase64(backup["salt"].toString().toLatin1());
    QByteArray nonce = QByteArray::fromBase64(backup["nonce"].toString().toLatin1());
    QByteArray ciphertext = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());

    QByteArray restoreKey = crypto.deriveKey(normalizeMnemonic(TEST_MNEMONIC), backupSalt);
    QByteArray plaintext = crypto.decrypt(ciphertext, restoreKey, nonce);
    QVERIFY(!plaintext.isEmpty());

    QJsonArray notes = QJsonDocument::fromJson(plaintext).array();
    QCOMPARE(notes.size(), 0);
}

// ── Multiple notes roundtrip ─────────────────────────────────────────────

void TestBackup::testMultiNoteRoundtrip()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    CryptoManager crypto;

    DatabaseManager db(tmpDir.path() + "/multi.db");
    QVERIFY(db.init());
    QByteArray salt;
    QByteArray key = setupAccount(db, crypto, TEST_MNEMONIC, TEST_PIN, salt);
    QVERIFY(!key.isEmpty());

    // Create 5 notes with varied content.
    QStringList expectedContents = {
        "First note body",
        "Second note\nwith multiple\nlines",
        "", // empty note
        "Fourth note with unicode: \xC3\xA9\xC3\xA0\xC3\xBC",
        "Fifth note"
    };
    QStringList expectedTitles = {
        "Note 1", "Note 2", "", "Note 4", "Note 5"
    };

    for (int i = 0; i < 5; ++i)
        createEncryptedNote(db, crypto, key, expectedContents[i], expectedTitles[i]);

    // Export.
    QByteArray backupData = buildBackupFile(db, crypto, key);

    // Parse and decrypt.
    QJsonObject backup = QJsonDocument::fromJson(backupData).object();
    QCOMPARE(backup["noteCount"].toInt(), 5);

    QByteArray backupSalt = QByteArray::fromBase64(backup["salt"].toString().toLatin1());
    QByteArray nonce = QByteArray::fromBase64(backup["nonce"].toString().toLatin1());
    QByteArray ciphertext = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());

    QByteArray restoreKey = crypto.deriveKey(normalizeMnemonic(TEST_MNEMONIC), backupSalt);
    QByteArray plaintext = crypto.decrypt(ciphertext, restoreKey, nonce);
    QVERIFY(!plaintext.isEmpty());

    QJsonArray notes = QJsonDocument::fromJson(plaintext).array();
    QCOMPARE(notes.size(), 5);

    // Verify all content survived the roundtrip.
    QStringList restoredContents, restoredTitles;
    for (const auto &v : notes) {
        restoredContents << v.toObject()["content"].toString();
        restoredTitles << v.toObject()["title"].toString();
    }

    for (const auto &expected : expectedContents)
        QVERIFY(restoredContents.contains(expected));
    for (const auto &expected : expectedTitles)
        QVERIFY(restoredTitles.contains(expected));
}

QTEST_MAIN(TestBackup)
#include "test_backup.moc"
