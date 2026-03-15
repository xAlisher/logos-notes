#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>

#include "core/CryptoManager.h"
#include "core/DatabaseManager.h"
#include "core/KeyManager.h"
#include "core/NotesBackend.h"
#include "core/SecureBuffer.h"

#include <sodium.h>

// Tests for backup export/import flows (Issue #25).
// Drives NotesBackend directly using QStandardPaths test mode
// so the backend writes to a temp location, not the real DB.

static const QString TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon "
    "abandon about";

static const QString TEST_PIN = "123456";

// Helper: parse JSON response from NotesBackend methods.
static QJsonObject parseJson(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

class TestBackup : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── Backend API tests ─────────────────────────────────────────────
    void testExportBackupReturnsJson();
    void testExportImportRoundtripViaBackend();
    void testCrossDeviceRestoreViaBackend();
    void testImportBackupInvalidJson();
    void testImportBackupUnsupportedVersion();
    void testImportBackupWrongMnemonic();
    void testImportBackupNonexistentFile();
    void testImportBackupCorruptCiphertext();
    void testImportBackupEmptyExport();
    void testExportBackupAutoPathGeneration();
    void testListBackups();
    void testMultiNoteRoundtripViaBackend();
    void testExportRequiresUnlock();
    void testImportRequiresUnlock();

private:
    // Import a mnemonic into a backend and return it in unlocked state.
    void importAndUnlock(NotesBackend &backend);

    // Wipe the test data directory between tests.
    void wipeTestData();
};

void TestBackup::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestBackup::cleanupTestCase()
{
    wipeTestData();
    QStandardPaths::setTestModeEnabled(false);
}

void TestBackup::wipeTestData()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(dataDir).removeRecursively();

    QString backupsDir = NotesBackend::backupsDir();
    QDir(backupsDir).removeRecursively();
}

void TestBackup::importAndUnlock(NotesBackend &backend)
{
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "note");
}

// ── Backend API tests ────────────────────────────────────────────────────

void TestBackup::testExportBackupReturnsJson()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create a note via backend.
    QString createResult = backend.createNote();
    QJsonObject created = parseJson(createResult);
    int noteId = created["id"].toInt();
    QVERIFY(noteId > 0);

    backend.saveNote(noteId, "Test content for export");

    // Export via backend API.
    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/test.imnotes";
    QString result = backend.exportBackup(path);
    QJsonObject obj = parseJson(result);

    QVERIFY(obj["ok"].toBool());
    QCOMPARE(obj["noteCount"].toInt(), 1);
    QCOMPARE(obj["path"].toString(), path);

    // Verify the file was written and contains valid backup format.
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject backup = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QCOMPARE(backup["version"].toInt(), 1);
    QCOMPARE(backup["noteCount"].toInt(), 1);
    QVERIFY(backup.contains("salt"));
    QVERIFY(backup.contains("nonce"));
    QVERIFY(backup.contains("ciphertext"));
}

void TestBackup::testExportImportRoundtripViaBackend()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create two notes.
    QJsonObject n1 = parseJson(backend.createNote());
    QJsonObject n2 = parseJson(backend.createNote());
    backend.saveNote(n1["id"].toInt(), "First note body");
    backend.saveNote(n2["id"].toInt(), "Second note body");

    // Export.
    QTemporaryDir tmpDir;
    QString exportPath = tmpDir.path() + "/roundtrip.imnotes";
    QString exportResult = backend.exportBackup(exportPath);
    QVERIFY(parseJson(exportResult)["ok"].toBool());

    // Delete both notes to simulate fresh state.
    backend.deleteNote(n1["id"].toInt());
    backend.deleteNote(n2["id"].toInt());

    // Verify notes are gone.
    QJsonArray beforeImport = QJsonDocument::fromJson(
        backend.loadNotes().toUtf8()).array();
    QCOMPARE(beforeImport.size(), 0);

    // Import backup (same device, no mnemonic needed).
    QString importResult = backend.importBackup(exportPath);
    QJsonObject imported = parseJson(importResult);
    QVERIFY(imported["ok"].toBool());
    QCOMPARE(imported["imported"].toInt(), 2);

    // Verify notes are restored.
    QJsonArray afterImport = QJsonDocument::fromJson(
        backend.loadNotes().toUtf8()).array();
    QCOMPARE(afterImport.size(), 2);

    // Verify content is correct.
    QStringList contents;
    for (const auto &v : afterImport) {
        int id = v.toObject()["id"].toInt();
        contents << backend.loadNote(id);
    }
    QVERIFY(contents.contains("First note body"));
    QVERIFY(contents.contains("Second note body"));
}

void TestBackup::testCrossDeviceRestoreViaBackend()
{
    wipeTestData();

    // "Device A": create account, notes, export.
    {
        NotesBackend backendA;
        importAndUnlock(backendA);

        QJsonObject n = parseJson(backendA.createNote());
        backendA.saveNote(n["id"].toInt(), "Cross-device secret");

        QTemporaryDir tmpDir;
        QString exportPath = tmpDir.path() + "/cross.imnotes";
        QVERIFY(parseJson(backendA.exportBackup(exportPath))["ok"].toBool());

        // Copy the backup to a known location before backendA is destroyed.
        QFile::copy(exportPath, "/tmp/test_cross_device.imnotes");
    }

    // "Device B": wipe, create fresh account with same mnemonic, import.
    wipeTestData();
    {
        NotesBackend backendB;
        importAndUnlock(backendB);

        // Import with mnemonic (cross-device: different salt).
        QString importResult = backendB.importBackup(
            "/tmp/test_cross_device.imnotes", TEST_MNEMONIC);
        QJsonObject imported = parseJson(importResult);
        QVERIFY(imported["ok"].toBool());
        QCOMPARE(imported["imported"].toInt(), 1);

        // Verify content.
        QJsonArray notes = QJsonDocument::fromJson(
            backendB.loadNotes().toUtf8()).array();
        QCOMPARE(notes.size(), 1);
        QString content = backendB.loadNote(notes[0].toObject()["id"].toInt());
        QCOMPARE(content, "Cross-device secret");
    }

    QFile::remove("/tmp/test_cross_device.imnotes");
}

void TestBackup::testImportBackupInvalidJson()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/bad.imnotes";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not json");
    f.close();

    QString result = backend.importBackup(path);
    QJsonObject obj = parseJson(result);
    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("Invalid backup format"));
}

void TestBackup::testImportBackupUnsupportedVersion()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QJsonObject backup;
    backup["version"] = 99;
    backup["salt"] = "AAAA";
    backup["nonce"] = "AAAA";
    backup["ciphertext"] = "AAAA";

    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/v99.imnotes";
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(backup).toJson(QJsonDocument::Compact));
    f.close();

    QString result = backend.importBackup(path);
    QJsonObject obj = parseJson(result);
    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("Unsupported backup version"));
}

void TestBackup::testImportBackupWrongMnemonic()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create and export a note.
    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "Secret");

    QTemporaryDir tmpDir;
    QString exportPath = tmpDir.path() + "/export.imnotes";
    QVERIFY(parseJson(backend.exportBackup(exportPath))["ok"].toBool());

    // Wipe and re-import with a different mnemonic.
    wipeTestData();
    NotesBackend backend2;
    QString wrongMnemonic = "abandon abandon abandon abandon abandon abandon "
                            "abandon abandon abandon abandon abandon abandon "
                            "abandon abandon abandon abandon abandon abandon "
                            "abandon abandon abandon abandon abandon art";
    backend2.importMnemonic(wrongMnemonic, TEST_PIN, TEST_PIN);
    QCOMPARE(backend2.currentScreen(), "note");

    QString result = backend2.importBackup(exportPath, wrongMnemonic);
    QJsonObject obj = parseJson(result);
    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("Cannot decrypt"));
}

void TestBackup::testImportBackupNonexistentFile()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QString result = backend.importBackup("/tmp/does_not_exist_12345.imnotes");
    QJsonObject obj = parseJson(result);
    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("Cannot read file"));
}

void TestBackup::testImportBackupCorruptCiphertext()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "Content to corrupt");

    QTemporaryDir tmpDir;
    QString exportPath = tmpDir.path() + "/corrupt.imnotes";
    QVERIFY(parseJson(backend.exportBackup(exportPath))["ok"].toBool());

    // Corrupt the ciphertext.
    QFile f(exportPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonObject backup = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QByteArray ct = QByteArray::fromBase64(backup["ciphertext"].toString().toLatin1());
    ct[0] = ct[0] ^ 0xFF;
    backup["ciphertext"] = QString::fromLatin1(ct.toBase64());

    QString corruptPath = tmpDir.path() + "/corrupt2.imnotes";
    QFile f2(corruptPath);
    QVERIFY(f2.open(QIODevice::WriteOnly));
    f2.write(QJsonDocument(backup).toJson(QJsonDocument::Compact));
    f2.close();

    QString result = backend.importBackup(corruptPath);
    QJsonObject obj = parseJson(result);
    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("Cannot decrypt"));
}

void TestBackup::testImportBackupEmptyExport()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Export with no notes.
    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/empty.imnotes";
    QString result = backend.exportBackup(path);
    QJsonObject exported = parseJson(result);
    QVERIFY(exported["ok"].toBool());
    QCOMPARE(exported["noteCount"].toInt(), 0);

    // Import empty backup.
    QString importResult = backend.importBackup(path);
    QJsonObject imported = parseJson(importResult);
    QVERIFY(imported["ok"].toBool());
    QCOMPARE(imported["imported"].toInt(), 0);
}

void TestBackup::testExportBackupAutoPathGeneration()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "Auto-export test");

    QString result = backend.exportBackupAuto();
    QJsonObject obj = parseJson(result);
    QVERIFY(obj["ok"].toBool());
    QCOMPARE(obj["noteCount"].toInt(), 1);

    // Path should be in the backups directory.
    QString path = obj["path"].toString();
    QVERIFY(path.startsWith(NotesBackend::backupsDir()));
    QVERIFY(path.endsWith(".imnotes"));

    // File should exist.
    QVERIFY(QFile::exists(path));
}

void TestBackup::testListBackups()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // No backups yet.
    QJsonArray emptyList = QJsonDocument::fromJson(
        backend.listBackups().toUtf8()).array();
    QCOMPARE(emptyList.size(), 0);

    // Create a note and export once via auto, once via explicit path.
    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "Backup list test");

    backend.exportBackupAuto();

    // Write a second backup file manually to avoid timestamp collision.
    QString secondPath = NotesBackend::backupsDir() + "/manual_backup.imnotes";
    backend.exportBackup(secondPath);

    QJsonArray list = QJsonDocument::fromJson(
        backend.listBackups().toUtf8()).array();
    QCOMPARE(list.size(), 2);

    // Each entry should have name and path.
    for (const auto &v : list) {
        QJsonObject entry = v.toObject();
        QVERIFY(entry.contains("name"));
        QVERIFY(entry.contains("path"));
        QVERIFY(entry["name"].toString().endsWith(".imnotes"));
    }
}

void TestBackup::testMultiNoteRoundtripViaBackend()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create 4 notes with varied content.
    QStringList contents = {
        "First note body",
        "Second note\nwith multiple\nlines",
        "Third note with unicode: \xC3\xA9\xC3\xA0\xC3\xBC",
        "Fourth note"
    };

    QList<int> noteIds;
    for (const auto &content : contents) {
        QJsonObject n = parseJson(backend.createNote());
        int id = n["id"].toInt();
        noteIds << id;
        backend.saveNote(id, content);
    }

    // Export.
    QTemporaryDir tmpDir;
    QString exportPath = tmpDir.path() + "/multi.imnotes";
    QVERIFY(parseJson(backend.exportBackup(exportPath))["ok"].toBool());

    // Delete all notes.
    for (int id : noteIds)
        backend.deleteNote(id);

    // Import.
    QString importResult = backend.importBackup(exportPath);
    QJsonObject imported = parseJson(importResult);
    QVERIFY(imported["ok"].toBool());
    QCOMPARE(imported["imported"].toInt(), 4);

    // Verify all content survived.
    QJsonArray restored = QJsonDocument::fromJson(
        backend.loadNotes().toUtf8()).array();
    QCOMPARE(restored.size(), 4);

    QStringList restoredContents;
    for (const auto &v : restored) {
        int id = v.toObject()["id"].toInt();
        restoredContents << backend.loadNote(id);
    }
    for (const auto &expected : contents)
        QVERIFY(restoredContents.contains(expected));
}

void TestBackup::testExportRequiresUnlock()
{
    wipeTestData();
    NotesBackend backend;
    // Don't import — backend is not unlocked.

    QString result = backend.exportBackup("/tmp/should_not_exist.imnotes");
    QJsonObject obj = parseJson(result);
    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("Not unlocked"));
    QVERIFY(!QFile::exists("/tmp/should_not_exist.imnotes"));
}

void TestBackup::testImportRequiresUnlock()
{
    wipeTestData();
    NotesBackend backend;

    QString result = backend.importBackup("/tmp/any_file.imnotes");
    QJsonObject obj = parseJson(result);
    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("Not unlocked"));
}

QTEST_MAIN(TestBackup)
#include "test_backup.moc"
