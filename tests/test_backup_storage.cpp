#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>

#include "core/NotesBackend.h"
#include "core/StorageClient.h"

// Integration test: NotesBackend.exportBackup → StorageClient.uploadFile →
// StorageClient.downloadToFile → NotesBackend.importBackup
//
// Verifies the two layers compose correctly without a running storage_module.
// Uses MockStorageTransport from the same pattern as test_storage_client.cpp.

static const QString TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon "
    "abandon about";
static const QString TEST_PIN = "123456";

// ── Mock transport (no SDK linkage needed) ────────────────────────────────────

class MockStorageTransport : public StorageTransport
{
public:
    bool connected = true;

    struct UploadCall   { QUrl url; int chunkSize; };
    struct DownloadCall { QString cid; QUrl destUrl; bool localOnly; int chunkSize; };

    QList<UploadCall>   uploadCalls;
    QList<DownloadCall> downloadCalls;
    EventCallback       eventCb;

    bool isConnected() const override { return connected; }

    void uploadUrl(const QUrl& url, int chunkSize) override {
        uploadCalls.append({url, chunkSize});
    }

    void downloadToUrl(const QString& cid, const QUrl& url,
                       bool localOnly, int chunkSize) override {
        downloadCalls.append({cid, url, localOnly, chunkSize});
    }

    void subscribeEventResponse(EventCallback cb) override {
        eventCb = std::move(cb);
    }

    void fireUploadDone(const QString& cid) {
        if (eventCb) eventCb(QStringLiteral("storageUploadDone"), {cid});
    }
    void fireDownloadDone() {
        if (eventCb) eventCb(QStringLiteral("storageDownloadDone"), {});
    }
    void fireDownloadFailed(const QString& error) {
        if (eventCb) eventCb(QStringLiteral("storageDownloadDone"), {error});
    }
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static QJsonObject parseJson(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestBackupStorage : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Core flow
    void testExportThenUpload();
    void testDownloadThenImport();
    void testFullRoundtrip();

    // Failure paths
    void testUploadUnavailableDoesNotBlockExport();
    void testDownloadFailureReportsError();
    void testUploadReturnsEmptyCidOnStorageError();

private:
    void wipeTestData();
    void importAndUnlock(NotesBackend& backend);
};

void TestBackupStorage::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestBackupStorage::cleanupTestCase()
{
    wipeTestData();
    QStandardPaths::setTestModeEnabled(false);
}

void TestBackupStorage::wipeTestData()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(dataDir).removeRecursively();
    QDir(NotesBackend::backupsDir()).removeRecursively();
}

void TestBackupStorage::importAndUnlock(NotesBackend& backend)
{
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), QStringLiteral("note"));
}

// ── testExportThenUpload ──────────────────────────────────────────────────────
// NotesBackend writes an encrypted backup file; StorageClient uploads it.
// The file that was uploaded must be the one NotesBackend wrote.

void TestBackupStorage::testExportThenUpload()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "upload test content");

    QTemporaryDir tmpDir;
    const QString exportPath = tmpDir.path() + "/upload_test.imnotes";
    QJsonObject exported = parseJson(backend.exportBackup(exportPath));
    QVERIFY(exported["ok"].toBool());
    QCOMPARE(exported["noteCount"].toInt(), 1);
    QVERIFY(QFile::exists(exportPath));

    // Upload the exported file via StorageClient.
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient storage(std::move(mock));
    storage.setTimeoutMs(0);  // disable timeout for synchronous mock

    QString receivedCid;
    QString receivedError;
    bool uploadDone = false;

    storage.uploadFile(exportPath,
        [&](const QString& cid, const QString& err) {
            receivedCid   = cid;
            receivedError = err;
            uploadDone    = true;
        });

    // Verify transport was called with the correct file.
    QCOMPARE(mockRaw->uploadCalls.size(), 1);
    QCOMPARE(mockRaw->uploadCalls[0].url.toLocalFile(), exportPath);

    // Simulate storage_module completing the upload.
    mockRaw->fireUploadDone(QStringLiteral("zDvZBackupCid123"));

    QVERIFY(uploadDone);
    QCOMPARE(receivedCid,   QStringLiteral("zDvZBackupCid123"));
    QVERIFY(receivedError.isEmpty());
}

// ── testDownloadThenImport ────────────────────────────────────────────────────
// StorageClient downloads a backup; NotesBackend imports it.
// Simulates receiving a known CID and restoring notes from it.

void TestBackupStorage::testDownloadThenImport()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // 1. Export a real backup so the file has valid ciphertext.
    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "download test content");

    QTemporaryDir srcDir;
    const QString srcPath = srcDir.path() + "/src.imnotes";
    QVERIFY(parseJson(backend.exportBackup(srcPath))["ok"].toBool());

    // 2. Use StorageClient to "download" to a destination path.
    //    (Mock simply confirms the call; the test then copies the file
    //     to simulate what a real download would produce.)
    QTemporaryDir dstDir;
    const QString dstPath = dstDir.path() + "/restored.imnotes";

    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient storage(std::move(mock));
    storage.setTimeoutMs(0);

    QString downloadError;
    bool downloadDone = false;

    const QString fakeCid = QStringLiteral("zDvZKnownCid456");
    storage.downloadToFile(fakeCid, dstPath,
        [&](const QString& err) {
            downloadError = err;
            downloadDone  = true;
        });

    QCOMPARE(mockRaw->downloadCalls.size(), 1);
    QCOMPARE(mockRaw->downloadCalls[0].cid, fakeCid);
    QCOMPARE(mockRaw->downloadCalls[0].destUrl.toLocalFile(), dstPath);

    // Simulate the downloaded file appearing (copy from the real export).
    QFile::copy(srcPath, dstPath);

    mockRaw->fireDownloadDone();

    QVERIFY(downloadDone);
    QVERIFY(downloadError.isEmpty());

    // 3. Import the downloaded backup — notes must be restored.
    backend.deleteNote(n["id"].toInt());
    QJsonArray before = QJsonDocument::fromJson(backend.loadNotes().toUtf8()).array();
    QCOMPARE(before.size(), 0);

    QJsonObject imported = parseJson(backend.importBackup(dstPath));
    QVERIFY(imported["ok"].toBool());
    QCOMPARE(imported["imported"].toInt(), 1);

    QJsonArray after = QJsonDocument::fromJson(backend.loadNotes().toUtf8()).array();
    QCOMPARE(after.size(), 1);
    const QString content = backend.loadNote(after[0].toObject()["id"].toInt());
    QCOMPARE(content, QStringLiteral("download test content"));
}

// ── testFullRoundtrip ─────────────────────────────────────────────────────────
// export → upload → (transmit CID) → download → import
// End-to-end chain with two notes and cross-device mnemonic restore.

void TestBackupStorage::testFullRoundtrip()
{
    wipeTestData();

    QTemporaryDir tmpDir;
    QString exportPath;

    // --- Phase 1: create, export, upload ---
    {
        NotesBackend backend;
        importAndUnlock(backend);

        QJsonObject n1 = parseJson(backend.createNote());
        QJsonObject n2 = parseJson(backend.createNote());
        backend.saveNote(n1["id"].toInt(), "Storage roundtrip note one");
        backend.saveNote(n2["id"].toInt(), "Storage roundtrip note two");

        exportPath = tmpDir.path() + "/full_roundtrip.imnotes";
        QVERIFY(parseJson(backend.exportBackup(exportPath))["ok"].toBool());
    }

    auto uploadMock = std::make_unique<MockStorageTransport>();
    auto* uploadRaw = uploadMock.get();
    StorageClient uploader(std::move(uploadMock));
    uploader.setTimeoutMs(0);

    QString cid;
    bool uploadDone = false;
    uploader.uploadFile(exportPath,
        [&](const QString& c, const QString& err) {
            QVERIFY(err.isEmpty());
            cid = c;
            uploadDone = true;
        });
    uploadRaw->fireUploadDone(QStringLiteral("zDvZRoundtripCid789"));
    QVERIFY(uploadDone);
    QVERIFY(!cid.isEmpty());

    // --- Phase 2: download to fresh path ---
    const QString dstPath = tmpDir.path() + "/restored_roundtrip.imnotes";

    auto downloadMock = std::make_unique<MockStorageTransport>();
    auto* downloadRaw = downloadMock.get();
    StorageClient downloader(std::move(downloadMock));
    downloader.setTimeoutMs(0);

    bool downloadDone = false;
    downloader.downloadToFile(cid, dstPath,
        [&](const QString& err) {
            QVERIFY(err.isEmpty());
            downloadDone = true;
        });

    // Simulate file appearing at destination.
    QFile::copy(exportPath, dstPath);
    downloadRaw->fireDownloadDone();
    QVERIFY(downloadDone);

    // --- Phase 3: import on fresh backend ---
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN, dstPath);
        QCOMPARE(backend.currentScreen(), QStringLiteral("note"));

        QJsonArray notes = QJsonDocument::fromJson(backend.loadNotes().toUtf8()).array();
        QCOMPARE(notes.size(), 2);

        QStringList contents;
        for (const auto& v : notes) {
            int id = v.toObject()["id"].toInt();
            contents << backend.loadNote(id);
        }
        QVERIFY(contents.contains(QStringLiteral("Storage roundtrip note one")));
        QVERIFY(contents.contains(QStringLiteral("Storage roundtrip note two")));
    }
}

// ── testUploadUnavailableDoesNotBlockExport ──────────────────────────────────
// If storage_module is offline, exportBackup still succeeds locally.
// The two operations are independent; upload failure must not corrupt the file.

void TestBackupStorage::testUploadUnavailableDoesNotBlockExport()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QJsonObject n = parseJson(backend.createNote());
    backend.saveNote(n["id"].toInt(), "offline note");

    QTemporaryDir tmpDir;
    const QString exportPath = tmpDir.path() + "/offline.imnotes";
    QJsonObject exported = parseJson(backend.exportBackup(exportPath));
    QVERIFY(exported["ok"].toBool());
    QVERIFY(QFile::exists(exportPath));

    // Storage unavailable.
    auto mock = std::make_unique<MockStorageTransport>();
    mock->connected = false;
    StorageClient storage(std::move(mock));

    QString uploadError;
    storage.uploadFile(exportPath,
        [&](const QString&, const QString& err) { uploadError = err; });

    // Upload failed but the local file is still intact.
    QVERIFY(!uploadError.isEmpty());
    QVERIFY(QFile::exists(exportPath));
    QJsonObject reimported = parseJson(backend.importBackup(exportPath));
    QVERIFY(reimported["ok"].toBool());
}

// ── testDownloadFailureReportsError ──────────────────────────────────────────
// If storage_module reports a download failure, the error propagates correctly.

void TestBackupStorage::testDownloadFailureReportsError()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient storage(std::move(mock));
    storage.setTimeoutMs(0);

    QTemporaryDir tmpDir;
    QString receivedError;
    bool called = false;

    storage.downloadToFile(QStringLiteral("zDvZMissing"),
                           tmpDir.filePath("out.imnotes"),
        [&](const QString& err) {
            receivedError = err;
            called = true;
        });

    mockRaw->fireDownloadFailed(QStringLiteral("not found in swarm"));

    QVERIFY(called);
    QCOMPARE(receivedError, QStringLiteral("not found in swarm"));
}

// ── testUploadReturnsEmptyCidOnStorageError ───────────────────────────────────
// If the upload event returns no CID, the error path fires.

void TestBackupStorage::testUploadReturnsEmptyCidOnStorageError()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient storage(std::move(mock));
    storage.setTimeoutMs(0);

    QTemporaryDir tmpDir;
    const QString path = tmpDir.filePath("dummy.imnotes");
    QFile f(path); f.open(QIODevice::WriteOnly); f.write("x"); f.close();

    QString receivedCid;
    QString receivedError;
    storage.uploadFile(path,
        [&](const QString& cid, const QString& err) {
            receivedCid   = cid;
            receivedError = err;
        });

    // Fire upload-done with empty CID (storage error scenario).
    if (mockRaw->eventCb)
        mockRaw->eventCb(QStringLiteral("storageUploadDone"), {});

    QVERIFY(receivedCid.isEmpty());
    QVERIFY(!receivedError.isEmpty());
}

QTEST_MAIN(TestBackupStorage)
#include "test_backup_storage.moc"
