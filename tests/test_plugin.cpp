#include <QtTest/QtTest>
#include <QStandardPaths>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "plugin/NotesPlugin.h"

// Tests for NotesPlugin shell contract (Issue #27).
// NotesPlugin is the only surface QML sees via logos.callModule.
// All methods return JSON strings; these tests verify the format
// and passthrough behavior.

static const QString TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon "
    "abandon about";

static const QString TEST_PIN = "123456";

static QJsonObject parseJson(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

class TestPlugin : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── isInitialized ────────────────────────────────────────────────
    void testIsInitializedBeforeImport();
    void testIsInitializedAfterImport();
    void testIsInitializedAfterReset();

    // ── importMnemonic ───────────────────────────────────────────────
    void testImportMnemonicSuccess();
    void testImportMnemonicInvalidPhrase();
    void testImportMnemonicMismatchedPins();

    // ── unlockWithPin ────────────────────────────────────────────────
    void testUnlockWithPinSuccess();
    void testUnlockWithPinWrongPin();

    // ── Note CRUD passthrough ────────────────────────────────────────
    void testNoteCrudPassthrough();

    // ── Session management ───────────────────────────────────────────
    void testLockSession();
    void testResetAndWipe();

    // ── Fingerprint ──────────────────────────────────────────────────
    void testGetAccountFingerprint();

    // ── JSON escaping ────────────────────────────────────────────────
    void testErrorJsonEscaping();

private:
    void wipeTestData();
};

void TestPlugin::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestPlugin::cleanupTestCase()
{
    wipeTestData();
    QStandardPaths::setTestModeEnabled(false);
}

void TestPlugin::wipeTestData()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(dataDir).removeRecursively();

    QString backupsDir = NotesBackend::backupsDir();
    QDir(backupsDir).removeRecursively();
}

// ── isInitialized ────────────────────────────────────────────────────────

void TestPlugin::testIsInitializedBeforeImport()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();

    QCOMPARE(plugin.isInitialized(), QStringLiteral("false"));
}

void TestPlugin::testIsInitializedAfterImport()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();

    QString result = plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QJsonObject obj = parseJson(result);
    QVERIFY(obj["success"].toBool());

    QCOMPARE(plugin.isInitialized(), QStringLiteral("true"));
}

void TestPlugin::testIsInitializedAfterReset()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();

    plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(plugin.isInitialized(), QStringLiteral("true"));

    plugin.resetAndWipe();
    QCOMPARE(plugin.isInitialized(), QStringLiteral("false"));
}

// ── importMnemonic ───────────────────────────────────────────────────────

void TestPlugin::testImportMnemonicSuccess()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();

    QString result = plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QJsonObject obj = parseJson(result);

    QVERIFY(obj.contains("success"));
    QVERIFY(obj["success"].toBool());
    // No error key on success.
    QVERIFY(!obj.contains("error"));
}

void TestPlugin::testImportMnemonicInvalidPhrase()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();

    QString result = plugin.importMnemonic(
        "invalid words that are not bip39", TEST_PIN, TEST_PIN);
    QJsonObject obj = parseJson(result);

    QVERIFY(obj.contains("error"));
    QVERIFY(!obj["error"].toString().isEmpty());
    QVERIFY(!obj.contains("success"));
}

void TestPlugin::testImportMnemonicMismatchedPins()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();

    QString result = plugin.importMnemonic(TEST_MNEMONIC, "123456", "654321");
    QJsonObject obj = parseJson(result);

    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("PIN"));
}

// ── unlockWithPin ────────────────────────────────────────────────────────

void TestPlugin::testUnlockWithPinSuccess()
{
    wipeTestData();

    // Import first to create account.
    {
        NotesPlugin plugin;
        plugin.initialize();
        plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    // New plugin instance — simulates app restart.
    NotesPlugin plugin;
    plugin.initialize();

    // Should be on unlock screen (account exists but not unlocked).
    QCOMPARE(plugin.isInitialized(), QStringLiteral("true"));

    QString result = plugin.unlockWithPin(TEST_PIN);
    QJsonObject obj = parseJson(result);

    QVERIFY(obj.contains("success"));
    QVERIFY(obj["success"].toBool());
    QVERIFY(!obj.contains("error"));
}

void TestPlugin::testUnlockWithPinWrongPin()
{
    wipeTestData();

    {
        NotesPlugin plugin;
        plugin.initialize();
        plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    NotesPlugin plugin;
    plugin.initialize();

    QString result = plugin.unlockWithPin("999999");
    QJsonObject obj = parseJson(result);

    QVERIFY(obj.contains("error"));
    QVERIFY(obj["error"].toString().contains("Wrong PIN"));
}

// ── Note CRUD passthrough ────────────────────────────────────────────────

void TestPlugin::testNoteCrudPassthrough()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();
    plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);

    // createNote — returns JSON with "id"
    QString createResult = plugin.createNote();
    QJsonObject created = parseJson(createResult);
    QVERIFY(created.contains("id"));
    int noteId = created["id"].toInt();
    QVERIFY(noteId > 0);

    // saveNote — returns "ok"
    QString saveResult = plugin.saveNote(noteId, "Hello from plugin test");
    QCOMPARE(saveResult, QStringLiteral("ok"));

    // loadNote — returns decrypted plaintext (not JSON)
    QString loaded = plugin.loadNote(noteId);
    QCOMPARE(loaded, QStringLiteral("Hello from plugin test"));

    // loadNotes — returns JSON array
    QString notesJson = plugin.loadNotes();
    QJsonArray notes = QJsonDocument::fromJson(notesJson.toUtf8()).array();
    QVERIFY(notes.size() >= 1);

    // Find our note in the list.
    bool found = false;
    for (const auto &v : notes) {
        QJsonObject entry = v.toObject();
        if (entry["id"].toInt() == noteId) {
            found = true;
            QVERIFY(entry.contains("title"));
            QVERIFY(entry.contains("updatedAt"));
            break;
        }
    }
    QVERIFY(found);

    // deleteNote — returns "ok"
    QString deleteResult = plugin.deleteNote(noteId);
    QCOMPARE(deleteResult, QStringLiteral("ok"));

    // Verify note is gone.
    QJsonArray afterDelete = QJsonDocument::fromJson(
        plugin.loadNotes().toUtf8()).array();
    for (const auto &v : afterDelete) {
        QVERIFY(v.toObject()["id"].toInt() != noteId);
    }
}

// ── Session management ───────────────────────────────────────────────────

void TestPlugin::testLockSession()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();
    plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);

    // Lock returns "ok".
    QString result = plugin.lockSession();
    QCOMPARE(result, QStringLiteral("ok"));

    // After lock, isInitialized is still true (account exists).
    QCOMPARE(plugin.isInitialized(), QStringLiteral("true"));

    // But note operations should fail (key wiped).
    QString createResult = plugin.createNote();
    QVERIFY(createResult.isEmpty());
}

void TestPlugin::testResetAndWipe()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();
    plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);

    QCOMPARE(plugin.isInitialized(), QStringLiteral("true"));

    QString result = plugin.resetAndWipe();
    QCOMPARE(result, QStringLiteral("ok"));

    QCOMPARE(plugin.isInitialized(), QStringLiteral("false"));
}

// ── Fingerprint ──────────────────────────────────────────────────────────

void TestPlugin::testGetAccountFingerprint()
{
    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();

    // Before import — empty.
    QVERIFY(plugin.getAccountFingerprint().isEmpty());

    plugin.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);

    QString fp = plugin.getAccountFingerprint();
    QCOMPARE(fp.length(), 64);

    // Verify it's valid hex.
    QRegularExpression hexRegex("^[0-9A-F]{64}$");
    QVERIFY(hexRegex.match(fp).hasMatch());
}

// ── JSON escaping ────────────────────────────────────────────────────────

void TestPlugin::testErrorJsonEscaping()
{
    // NotesPlugin::errorJson must produce valid JSON even when
    // the error message contains special characters.
    // We trigger this by importing with an invalid mnemonic whose
    // error message is controlled by the backend.

    wipeTestData();
    NotesPlugin plugin;
    plugin.initialize();

    // The backend produces an error message; verify the plugin
    // wraps it in valid JSON.
    QString result = plugin.importMnemonic(
        "invalid mnemonic phrase", TEST_PIN, TEST_PIN);

    // Must be parseable JSON.
    QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
    QVERIFY(!doc.isNull());
    QVERIFY(doc.isObject());

    QJsonObject obj = doc.object();
    QVERIFY(obj.contains("error"));
    QVERIFY(!obj["error"].toString().isEmpty());

    // Now test with a mnemonic that might produce quotes in error.
    // Use a mnemonic with special chars — backend will say "Invalid recovery phrase"
    // but the important thing is JSON validity.
    QString result2 = plugin.importMnemonic(
        "word\"with\\quotes", TEST_PIN, TEST_PIN);

    QJsonDocument doc2 = QJsonDocument::fromJson(result2.toUtf8());
    QVERIFY2(!doc2.isNull(),
             qPrintable(QString("Invalid JSON from errorJson: %1").arg(result2)));
    QVERIFY(doc2.isObject());
    QVERIFY(doc2.object().contains("error"));
}

QTEST_MAIN(TestPlugin)
#include "test_plugin.moc"
