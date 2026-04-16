#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/NotesBackend.h"
#include "core/StorageClient.h"

// Tests for issue #72: auto-backup on save with CID tracking.
//
// NotesBackend now arms a 30s debounce timer after each saveNote() in a
// Keycard session. On timer fire it writes a local backup file then uploads
// via StorageClient. CID and timestamp are persisted in meta["backup_cid"]
// and meta["backup_timestamp"]. Mnemonic sessions are completely unaffected.

// ── Deterministic test key (32 bytes, hex-encoded) ───────────────────────────

static const QString TEST_HEX_KEY =
    "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";

// ── Mock transport ────────────────────────────────────────────────────────────

class MockStorageTransport : public StorageTransport
{
public:
    bool connected = true;
    int  uploadCalls = 0;
    StorageTransport::EventCallback eventCb;

    bool isConnected() const override { return connected; }

    void uploadUrl(const QUrl&, int) override { ++uploadCalls; }

    void downloadToUrl(const QString&, const QUrl&, bool, int) override {}

    void subscribeEventResponse(EventCallback cb) override { eventCb = std::move(cb); }

    void fireUploadDone(const QString& cid) {
        if (eventCb) eventCb(QStringLiteral("storageUploadDone"), {cid});
    }
    void fireUploadFailed() {
        if (eventCb) eventCb(QStringLiteral("storageUploadDone"), {});
    }
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static QJsonObject parseJson(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestAutoBackup : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Keycard session — happy path
    void testDebounceFiresAfterSave();
    void testCidStoredOnSuccess();
    void testCidNotUpdatedOnFailure();
    void testMultipleRapidSavesCoalesce();
    void testTriggerBackupManual();
    void testGetBackupCidEmpty();
    void testGetBackupCidAfterUpload();

    // Mnemonic session — must be completely unaffected
    void testMnemonicSessionNoDebounce();
    void testMnemonicTriggerBackupReturnsError();
    void testMnemonicGetStorageStatusDisabled();

    // Storage unavailable
    void testStorageUnavailableStatus();
    void testStorageUnavailableNocrash();

    // Session fencing — HIGH finding regression tests (issue #72 round 2)
    void testWipeDuringUploadDiscardsCallback();
    void testLockDuringUploadDiscardsCallback();
    void testTriggerBackupUnavailableReturnsError();
    void testSetBackupCidPersists();
    void testSetBackupCidEmptyCidReturnsError();

private:
    void wipeTestData();

    // Returns a NotesBackend unlocked via keycard with a mock StorageClient.
    // The mock raw pointer is written to *mockOut if non-null.
    NotesBackend* makeKeycardBackend(MockStorageTransport** mockOut = nullptr);

    QList<NotesBackend*> m_backends;  // owned, deleted in cleanupTestCase
};

void TestAutoBackup::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestAutoBackup::cleanupTestCase()
{
    qDeleteAll(m_backends);
    m_backends.clear();
    wipeTestData();
    QStandardPaths::setTestModeEnabled(false);
}

void TestAutoBackup::wipeTestData()
{
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .removeRecursively();
    QDir(NotesBackend::backupsDir()).removeRecursively();
}

NotesBackend* TestAutoBackup::makeKeycardBackend(MockStorageTransport** mockOut)
{
    auto* backend = new NotesBackend;
    m_backends.append(backend);

    // Set a very short debounce so tests don't wait 30s.
    backend->setDebounceIntervalMs(50);

    // Import a keycard account.
    backend->importWithKeycardKey(TEST_HEX_KEY);
    Q_ASSERT(backend->currentScreen() == QStringLiteral("note"));

    // Inject mock StorageClient.
    auto mock = std::make_unique<MockStorageTransport>();
    if (mockOut)
        *mockOut = mock.get();

    backend->setStorageClient(
        std::make_unique<StorageClient>(std::move(mock)));

    return backend;
}

// ── testDebounceFiresAfterSave ────────────────────────────────────────────────

void TestAutoBackup::testDebounceFiresAfterSave()
{
    wipeTestData();
    MockStorageTransport* mock = nullptr;
    NotesBackend* backend = makeKeycardBackend(&mock);

    QJsonObject n = parseJson(backend->createNote());
    backend->saveNote(n["id"].toInt(), "debounce test");

    // Wait for debounce (set to 50ms in makeKeycardBackend).
    QTest::qWait(200);

    QCOMPARE(mock->uploadCalls, 1);
}

// ── testCidStoredOnSuccess ────────────────────────────────────────────────────

void TestAutoBackup::testCidStoredOnSuccess()
{
    wipeTestData();
    MockStorageTransport* mock = nullptr;
    NotesBackend* backend = makeKeycardBackend(&mock);

    QJsonObject n = parseJson(backend->createNote());
    backend->saveNote(n["id"].toInt(), "cid test");

    QTest::qWait(200);
    QCOMPARE(mock->uploadCalls, 1);
    mock->fireUploadDone(QStringLiteral("zDvZTestCid001"));

    QJsonObject cidObj = parseJson(backend->getBackupCid());
    QCOMPARE(cidObj["cid"].toString(), QStringLiteral("zDvZTestCid001"));
    QVERIFY(!cidObj["timestamp"].toString().isEmpty());
    QCOMPARE(backend->getStorageStatus(), QStringLiteral("synced"));
}

// ── testCidNotUpdatedOnFailure ────────────────────────────────────────────────

void TestAutoBackup::testCidNotUpdatedOnFailure()
{
    wipeTestData();
    MockStorageTransport* mock = nullptr;
    NotesBackend* backend = makeKeycardBackend(&mock);

    QJsonObject n = parseJson(backend->createNote());
    backend->saveNote(n["id"].toInt(), "failure test");

    QTest::qWait(200);
    QCOMPARE(mock->uploadCalls, 1);

    // Simulate storage returning empty CID (failure).
    mock->fireUploadFailed();

    // backup_cid must remain empty (no stale CID written).
    QCOMPARE(backend->getBackupCid(), QStringLiteral("{}"));
    QCOMPARE(backend->getStorageStatus(), QStringLiteral("failed"));
}

// ── testMultipleRapidSavesCoalesce ────────────────────────────────────────────

void TestAutoBackup::testMultipleRapidSavesCoalesce()
{
    wipeTestData();
    MockStorageTransport* mock = nullptr;
    NotesBackend* backend = makeKeycardBackend(&mock);

    QJsonObject n = parseJson(backend->createNote());
    // Five rapid saves — debounce should collapse to one upload.
    for (int i = 0; i < 5; ++i)
        backend->saveNote(n["id"].toInt(), QString("rapid save %1").arg(i));

    QTest::qWait(300);

    QCOMPARE(mock->uploadCalls, 1);
}

// ── testTriggerBackupManual ───────────────────────────────────────────────────

void TestAutoBackup::testTriggerBackupManual()
{
    wipeTestData();
    MockStorageTransport* mock = nullptr;
    NotesBackend* backend = makeKeycardBackend(&mock);

    QJsonObject n = parseJson(backend->createNote());
    backend->saveNote(n["id"].toInt(), "manual trigger test");

    // triggerBackup() bypasses the debounce and fires immediately.
    QJsonObject result = parseJson(backend->triggerBackup());
    QVERIFY(result["success"].toBool());
    QCOMPARE(mock->uploadCalls, 1);
}

// ── testGetBackupCidEmpty ─────────────────────────────────────────────────────

void TestAutoBackup::testGetBackupCidEmpty()
{
    wipeTestData();
    NotesBackend backend;
    backend.importWithKeycardKey(TEST_HEX_KEY);
    QCOMPARE(backend.getBackupCid(), QStringLiteral("{}"));
}

// ── testGetBackupCidAfterUpload ───────────────────────────────────────────────

void TestAutoBackup::testGetBackupCidAfterUpload()
{
    wipeTestData();
    MockStorageTransport* mock = nullptr;
    NotesBackend* backend = makeKeycardBackend(&mock);

    QJsonObject n = parseJson(backend->createNote());
    backend->saveNote(n["id"].toInt(), "cid after upload");
    backend->triggerBackup();
    mock->fireUploadDone(QStringLiteral("zDvZResultCid999"));

    QJsonObject cidObj = parseJson(backend->getBackupCid());
    QCOMPARE(cidObj["cid"].toString(), QStringLiteral("zDvZResultCid999"));
}

// ── testMnemonicSessionNoDebounce ─────────────────────────────────────────────

void TestAutoBackup::testMnemonicSessionNoDebounce()
{
    wipeTestData();
    auto mock = std::make_unique<MockStorageTransport>();
    MockStorageTransport* mockRaw = mock.get();

    NotesBackend backend;
    backend.setDebounceIntervalMs(50);
    backend.importMnemonic(
        "abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon "
        "abandon about",
        "123456", "123456");
    QCOMPARE(backend.currentScreen(), QStringLiteral("note"));

    backend.setStorageClient(
        std::make_unique<StorageClient>(std::move(mock)));

    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "mnemonic save — must not upload");

    QTest::qWait(200);

    // No upload must have been triggered.
    QCOMPARE(mockRaw->uploadCalls, 0);
}

// ── testMnemonicTriggerBackupReturnsError ─────────────────────────────────────

void TestAutoBackup::testMnemonicTriggerBackupReturnsError()
{
    wipeTestData();
    NotesBackend backend;
    backend.importMnemonic(
        "abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon "
        "abandon about",
        "123456", "123456");
    QCOMPARE(backend.currentScreen(), QStringLiteral("note"));

    QJsonObject result = parseJson(backend.triggerBackup());
    QVERIFY(!result["error"].toString().isEmpty());
}

// ── testMnemonicGetStorageStatusDisabled ──────────────────────────────────────

void TestAutoBackup::testMnemonicGetStorageStatusDisabled()
{
    wipeTestData();
    NotesBackend backend;
    backend.importMnemonic(
        "abandon abandon abandon abandon abandon "
        "abandon abandon abandon abandon abandon "
        "abandon about",
        "123456", "123456");

    QCOMPARE(backend.getStorageStatus(), QStringLiteral("disabled"));
}

// ── testStorageUnavailableStatus ──────────────────────────────────────────────

void TestAutoBackup::testStorageUnavailableStatus()
{
    wipeTestData();
    auto mock = std::make_unique<MockStorageTransport>();
    mock->connected = false;

    NotesBackend backend;
    backend.setDebounceIntervalMs(50);
    backend.importWithKeycardKey(TEST_HEX_KEY);
    backend.setStorageClient(
        std::make_unique<StorageClient>(std::move(mock)));

    QCOMPARE(backend.getStorageStatus(), QStringLiteral("unavailable"));
}

// ── testStorageUnavailableNocrash ─────────────────────────────────────────────

void TestAutoBackup::testStorageUnavailableNocrash()
{
    wipeTestData();
    auto mock = std::make_unique<MockStorageTransport>();
    mock->connected = false;
    MockStorageTransport* mockRaw = mock.get();

    NotesBackend backend;
    backend.setDebounceIntervalMs(50);
    backend.importWithKeycardKey(TEST_HEX_KEY);
    backend.setStorageClient(
        std::make_unique<StorageClient>(std::move(mock)));

    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "unavailable storage test");

    QTest::qWait(200);

    // No upload attempt — storage was unavailable.
    QCOMPARE(mockRaw->uploadCalls, 0);
    QCOMPARE(backend.getStorageStatus(), QStringLiteral("unavailable"));
    // App must still be on the note screen (no crash, no state corruption).
    QCOMPARE(backend.currentScreen(), QStringLiteral("note"));
}

// ── testWipeDuringUploadDiscardsCallback ──────────────────────────────────────
// Start an upload, call resetAndWipe() before the callback fires.
// The callback must be discarded — backup_cid must remain empty in the fresh DB.

void TestAutoBackup::testWipeDuringUploadDiscardsCallback()
{
    wipeTestData();
    MockStorageTransport* mock = nullptr;
    NotesBackend* backend = makeKeycardBackend(&mock);

    QJsonObject n = parseJson(backend->createNote());
    backend->saveNote(n["id"].toInt(), "wipe during upload test");
    backend->triggerBackup();  // upload in flight
    QCOMPARE(mock->uploadCalls, 1);

    // Wipe while upload is in flight.
    backend->resetAndWipe();
    QCOMPARE(backend->currentScreen(), QStringLiteral("import"));

    // Now the old callback fires — must be discarded.
    mock->fireUploadDone(QStringLiteral("zDvZShouldBeDiscarded"));

    // Fresh DB must have no backup_cid.
    QCOMPARE(backend->getBackupCid(), QStringLiteral("{}"));
}

// ── testLockDuringUploadDiscardsCallback ──────────────────────────────────────
// Start an upload, lock() before callback fires. Callback must be discarded.

void TestAutoBackup::testLockDuringUploadDiscardsCallback()
{
    wipeTestData();
    MockStorageTransport* mock = nullptr;
    NotesBackend* backend = makeKeycardBackend(&mock);

    QJsonObject n = parseJson(backend->createNote());
    backend->saveNote(n["id"].toInt(), "lock during upload test");
    backend->triggerBackup();
    QCOMPARE(mock->uploadCalls, 1);

    // Lock while upload is in flight.
    backend->lock();

    // Old callback fires — must be discarded.
    mock->fireUploadDone(QStringLiteral("zDvZAlsoDiscarded"));

    // getStorageStatus must not reflect a stale "synced" from the discarded callback.
    // After lock, m_keySource is cleared → status is "disabled".
    QCOMPARE(backend->getStorageStatus(), QStringLiteral("disabled"));
}

// ── testTriggerBackupUnavailableReturnsError ──────────────────────────────────
// triggerBackup() when storage is unavailable must return an error, not success.

void TestAutoBackup::testTriggerBackupUnavailableReturnsError()
{
    wipeTestData();
    auto mock = std::make_unique<MockStorageTransport>();
    mock->connected = false;

    NotesBackend backend;
    backend.setDebounceIntervalMs(50);
    backend.importWithKeycardKey(TEST_HEX_KEY);
    backend.setStorageClient(
        std::make_unique<StorageClient>(std::move(mock)));

    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "unavailable trigger test");

    QJsonObject result = parseJson(backend.triggerBackup());
    QVERIFY(!result["error"].toString().isEmpty());
    QVERIFY(!result.contains("success"));
}

// ── testSetBackupCid ─────────────────────────────────────────────────────────
// setBackupCid() must persist the CID and timestamp and be readable via getBackupCid().

void TestAutoBackup::testSetBackupCidPersists()
{
    wipeTestData();
    NotesBackend backend;
    backend.importWithKeycardKey(TEST_HEX_KEY);

    QJsonObject setResult = parseJson(
        backend.setBackupCid(QStringLiteral("zDvZStashCid"), QStringLiteral("1712345678")));
    QVERIFY(setResult[QStringLiteral("ok")].toBool());

    QJsonObject cidObj = parseJson(backend.getBackupCid());
    QCOMPARE(cidObj[QStringLiteral("cid")].toString(), QStringLiteral("zDvZStashCid"));
    QCOMPARE(cidObj[QStringLiteral("timestamp")].toString(), QStringLiteral("1712345678"));
}

void TestAutoBackup::testSetBackupCidEmptyCidReturnsError()
{
    NotesBackend backend;
    QJsonObject result = parseJson(backend.setBackupCid({}, {}));
    QVERIFY(!result[QStringLiteral("error")].toString().isEmpty());
    QVERIFY(!result.contains(QStringLiteral("ok")));
}

QTEST_MAIN(TestAutoBackup)
#include "test_autobackup.moc"
