#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include <QUrl>

#include "core/StorageClient.h"

// Tests for StorageClient (Phase 1 of v2.0 — issue #71).
//
// Uses a MockStorageTransport that records calls and fires synthetic
// eventResponse callbacks, so these tests do not require a running
// storage_module or any Logos SDK linkage.

class MockStorageTransport : public StorageTransport
{
public:
    bool connected = true;

    struct UploadCall   { QUrl url; int chunkSize; };
    struct DownloadCall { QString cid; QUrl url; bool localOnly; int chunkSize; };

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

    // Test helpers — fire synthetic events
    void fireUploadDone(const QString& cid) {
        if (eventCb) eventCb(QStringLiteral("storageUploadDone"),
                             QVariantList{cid});
    }
    void fireDownloadDone() {
        if (eventCb) eventCb(QStringLiteral("storageDownloadDone"),
                             QVariantList{});
    }
    void fireDownloadFailed(const QString& error) {
        if (eventCb) eventCb(QStringLiteral("storageDownloadDone"),
                             QVariantList{error});
    }
    void fireStrayEvent(const QString& name) {
        if (eventCb) eventCb(name, QVariantList{QStringLiteral("zDvZOrphan")});
    }
};

class TestStorageClient : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // isAvailable
    void testIsAvailableWhenTransportConnected();
    void testIsAvailableWhenTransportDisconnected();
    void testIsAvailableWhenTransportNull();

    // uploadFile — success / validation
    void testUploadFileSuccess();
    void testUploadFileWithMissingFile();
    void testUploadFileWhenUnavailable();
    void testUploadFileCallsTransport();

    // uploadFile — concurrency & timeouts
    void testConcurrentUploadRejected();
    void testUploadCompletesThenAllowsNextUpload();
    void testUploadTimeoutFiresCallback();
    void testStrayUploadDoneIgnored();

    // downloadToFile — success / validation
    void testDownloadSuccess();
    void testDownloadWithEmptyCid();
    void testDownloadWithEmptyPath();
    void testDownloadWhenUnavailable();
    void testDownloadFailureFromEvent();

    // downloadToFile — concurrency & timeouts
    void testConcurrentDownloadRejected();
    void testDownloadCompletesThenAllowsNextDownload();
    void testDownloadTimeoutFiresCallback();
    void testStrayDownloadDoneIgnored();

    // Independence between upload/download slots
    void testUploadAndDownloadCanOverlap();

    // Destructor cleanup
    void testDestructorFailsPending();

private:
    QTemporaryDir m_tmpDir;
    QString m_existingFile;
};

void TestStorageClient::initTestCase()
{
    QVERIFY(m_tmpDir.isValid());
    m_existingFile = m_tmpDir.filePath("sample.bin");
    QFile f(m_existingFile);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello storage");
    f.close();
}

// ── isAvailable ──────────────────────────────────────────────────────────────

void TestStorageClient::testIsAvailableWhenTransportConnected()
{
    auto mock = std::make_unique<MockStorageTransport>();
    mock->connected = true;
    StorageClient client(std::move(mock));
    QVERIFY(client.isAvailable());
}

void TestStorageClient::testIsAvailableWhenTransportDisconnected()
{
    auto mock = std::make_unique<MockStorageTransport>();
    mock->connected = false;
    StorageClient client(std::move(mock));
    QVERIFY(!client.isAvailable());
}

void TestStorageClient::testIsAvailableWhenTransportNull()
{
    StorageClient client(nullptr);
    QVERIFY(!client.isAvailable());
}

// ── uploadFile success / validation ─────────────────────────────────────────

void TestStorageClient::testUploadFileSuccess()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    QString receivedCid;
    QString receivedError;
    bool called = false;

    client.uploadFile(m_existingFile,
        [&](const QString& cid, const QString& err) {
            receivedCid = cid;
            receivedError = err;
            called = true;
        });

    mockRaw->fireUploadDone(QStringLiteral("zDvZFakeCid123"));

    QVERIFY(called);
    QCOMPARE(receivedCid, QStringLiteral("zDvZFakeCid123"));
    QVERIFY(receivedError.isEmpty());
}

void TestStorageClient::testUploadFileWithMissingFile()
{
    auto mock = std::make_unique<MockStorageTransport>();
    StorageClient client(std::move(mock));

    QString receivedCid;
    QString receivedError;
    bool called = false;

    client.uploadFile(m_tmpDir.filePath("does-not-exist.bin"),
        [&](const QString& cid, const QString& err) {
            receivedCid = cid;
            receivedError = err;
            called = true;
        });

    QVERIFY(called);
    QVERIFY(receivedCid.isEmpty());
    QVERIFY(receivedError.contains(QStringLiteral("does not exist")));
}

void TestStorageClient::testUploadFileWhenUnavailable()
{
    auto mock = std::make_unique<MockStorageTransport>();
    mock->connected = false;
    StorageClient client(std::move(mock));

    QString receivedError;
    bool called = false;

    client.uploadFile(m_existingFile,
        [&](const QString& cid, const QString& err) {
            Q_UNUSED(cid);
            receivedError = err;
            called = true;
        });

    QVERIFY(called);
    QCOMPARE(receivedError, QStringLiteral("storage_module not available"));
}

void TestStorageClient::testUploadFileCallsTransport()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    client.uploadFile(m_existingFile,
        [](const QString&, const QString&) {});

    QCOMPARE(mockRaw->uploadCalls.size(), 1);
    QVERIFY(mockRaw->uploadCalls[0].url.isLocalFile());
    QCOMPARE(mockRaw->uploadCalls[0].url.toLocalFile(),
             QFileInfo(m_existingFile).absoluteFilePath());
    QVERIFY(mockRaw->uploadCalls[0].chunkSize > 0);
}

// ── uploadFile concurrency & timeouts ───────────────────────────────────────

void TestStorageClient::testConcurrentUploadRejected()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    QString firstCid, secondError;
    bool firstCalled = false, secondCalled = false;

    client.uploadFile(m_existingFile,
        [&](const QString& cid, const QString& err) {
            Q_UNUSED(err);
            firstCid = cid;
            firstCalled = true;
        });

    // Second upload while first is still pending.
    client.uploadFile(m_existingFile,
        [&](const QString& cid, const QString& err) {
            Q_UNUSED(cid);
            secondError = err;
            secondCalled = true;
        });

    QVERIFY(secondCalled);  // synchronous rejection
    QVERIFY(secondError.contains(QStringLiteral("busy")));
    QCOMPARE(mockRaw->uploadCalls.size(), 1);  // only first was dispatched
    QVERIFY(!firstCalled);  // first still pending

    // Finish the first upload.
    mockRaw->fireUploadDone(QStringLiteral("zDvZFirst"));
    QVERIFY(firstCalled);
    QCOMPARE(firstCid, QStringLiteral("zDvZFirst"));
}

void TestStorageClient::testUploadCompletesThenAllowsNextUpload()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    QString cid1, cid2;
    client.uploadFile(m_existingFile,
        [&](const QString& c, const QString&) { cid1 = c; });
    mockRaw->fireUploadDone(QStringLiteral("zDvZFirst"));

    // Second upload should be allowed now.
    client.uploadFile(m_existingFile,
        [&](const QString& c, const QString&) { cid2 = c; });
    mockRaw->fireUploadDone(QStringLiteral("zDvZSecond"));

    QCOMPARE(cid1, QStringLiteral("zDvZFirst"));
    QCOMPARE(cid2, QStringLiteral("zDvZSecond"));
    QCOMPARE(mockRaw->uploadCalls.size(), 2);
}

void TestStorageClient::testUploadTimeoutFiresCallback()
{
    auto mock = std::make_unique<MockStorageTransport>();
    StorageClient client(std::move(mock));
    client.setTimeoutMs(50);  // 50ms for fast test

    QString receivedError;
    bool called = false;

    client.uploadFile(m_existingFile,
        [&](const QString&, const QString& err) {
            receivedError = err;
            called = true;
        });

    // Don't fire any event — let the timer expire.
    QTest::qWait(200);

    QVERIFY(called);
    QVERIFY(receivedError.contains(QStringLiteral("timed out")));
}

void TestStorageClient::testStrayUploadDoneIgnored()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    // Ensure subscription is set up.
    bool called = false;
    client.uploadFile(m_existingFile,
        [&](const QString&, const QString&) { called = true; });
    mockRaw->fireUploadDone(QStringLiteral("zDvZOurs"));
    QVERIFY(called);

    // Stray event — no pending upload. Should be ignored, not crash.
    mockRaw->fireUploadDone(QStringLiteral("zDvZOrphan"));
    // (no assertion; if it crashed we'd fail)

    // Subsequent real upload should still work.
    QString cid2;
    client.uploadFile(m_existingFile,
        [&](const QString& c, const QString&) { cid2 = c; });
    mockRaw->fireUploadDone(QStringLiteral("zDvZSecond"));
    QCOMPARE(cid2, QStringLiteral("zDvZSecond"));
}

// ── downloadToFile success / validation ─────────────────────────────────────

void TestStorageClient::testDownloadSuccess()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    const QString destPath = m_tmpDir.filePath("downloaded.bin");
    const QString cid = QStringLiteral("zDvZSomeCid");

    QString receivedError;
    bool called = false;

    client.downloadToFile(cid, destPath,
        [&](const QString& err) {
            receivedError = err;
            called = true;
        });

    QCOMPARE(mockRaw->downloadCalls.size(), 1);
    QCOMPARE(mockRaw->downloadCalls[0].cid, cid);
    QCOMPARE(mockRaw->downloadCalls[0].url.toLocalFile(), destPath);
    QCOMPARE(mockRaw->downloadCalls[0].localOnly, true);

    mockRaw->fireDownloadDone();

    QVERIFY(called);
    QVERIFY(receivedError.isEmpty());
}

void TestStorageClient::testDownloadWithEmptyCid()
{
    auto mock = std::make_unique<MockStorageTransport>();
    StorageClient client(std::move(mock));

    QString receivedError;
    client.downloadToFile(QString(), m_tmpDir.filePath("out.bin"),
        [&](const QString& err) { receivedError = err; });

    QCOMPARE(receivedError, QStringLiteral("CID is empty"));
}

void TestStorageClient::testDownloadWithEmptyPath()
{
    auto mock = std::make_unique<MockStorageTransport>();
    StorageClient client(std::move(mock));

    QString receivedError;
    client.downloadToFile(QStringLiteral("zDvZ"), QString(),
        [&](const QString& err) { receivedError = err; });

    QCOMPARE(receivedError, QStringLiteral("Destination path is empty"));
}

void TestStorageClient::testDownloadWhenUnavailable()
{
    auto mock = std::make_unique<MockStorageTransport>();
    mock->connected = false;
    StorageClient client(std::move(mock));

    QString receivedError;
    client.downloadToFile(QStringLiteral("zDvZ"), m_tmpDir.filePath("out.bin"),
        [&](const QString& err) { receivedError = err; });

    QCOMPARE(receivedError, QStringLiteral("storage_module not available"));
}

void TestStorageClient::testDownloadFailureFromEvent()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    QString receivedError;
    bool called = false;

    client.downloadToFile(QStringLiteral("zDvZ"), m_tmpDir.filePath("out.bin"),
        [&](const QString& err) {
            receivedError = err;
            called = true;
        });

    mockRaw->fireDownloadFailed(QStringLiteral("network timeout"));

    QVERIFY(called);
    QCOMPARE(receivedError, QStringLiteral("network timeout"));
}

// ── downloadToFile concurrency & timeouts ───────────────────────────────────

void TestStorageClient::testConcurrentDownloadRejected()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    QString secondError;
    bool secondCalled = false;

    client.downloadToFile(QStringLiteral("zDvZFirst"),
                          m_tmpDir.filePath("out1.bin"),
        [](const QString&) {});

    client.downloadToFile(QStringLiteral("zDvZSecond"),
                          m_tmpDir.filePath("out2.bin"),
        [&](const QString& err) {
            secondError = err;
            secondCalled = true;
        });

    QVERIFY(secondCalled);
    QVERIFY(secondError.contains(QStringLiteral("busy")));
    QCOMPARE(mockRaw->downloadCalls.size(), 1);
}

void TestStorageClient::testDownloadCompletesThenAllowsNextDownload()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    int successCount = 0;
    client.downloadToFile(QStringLiteral("zDvZFirst"),
                          m_tmpDir.filePath("out1.bin"),
        [&](const QString& err) { if (err.isEmpty()) ++successCount; });
    mockRaw->fireDownloadDone();

    client.downloadToFile(QStringLiteral("zDvZSecond"),
                          m_tmpDir.filePath("out2.bin"),
        [&](const QString& err) { if (err.isEmpty()) ++successCount; });
    mockRaw->fireDownloadDone();

    QCOMPARE(successCount, 2);
    QCOMPARE(mockRaw->downloadCalls.size(), 2);
}

void TestStorageClient::testDownloadTimeoutFiresCallback()
{
    auto mock = std::make_unique<MockStorageTransport>();
    StorageClient client(std::move(mock));
    client.setTimeoutMs(50);

    QString receivedError;
    bool called = false;

    client.downloadToFile(QStringLiteral("zDvZ"), m_tmpDir.filePath("out.bin"),
        [&](const QString& err) {
            receivedError = err;
            called = true;
        });

    QTest::qWait(200);

    QVERIFY(called);
    QVERIFY(receivedError.contains(QStringLiteral("timed out")));
}

void TestStorageClient::testStrayDownloadDoneIgnored()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    // Need a successful cycle first to establish the subscription.
    client.downloadToFile(QStringLiteral("zDvZ"), m_tmpDir.filePath("out.bin"),
        [](const QString&) {});
    mockRaw->fireDownloadDone();

    // Stray event when no pending download — should be ignored.
    mockRaw->fireDownloadDone();

    // Next real download should still work.
    QString receivedError;
    client.downloadToFile(QStringLiteral("zDvZ2"), m_tmpDir.filePath("out2.bin"),
        [&](const QString& err) { receivedError = err; });
    mockRaw->fireDownloadDone();
    QVERIFY(receivedError.isEmpty());
}

// ── Independence between upload and download slots ─────────────────────────

void TestStorageClient::testUploadAndDownloadCanOverlap()
{
    auto mock = std::make_unique<MockStorageTransport>();
    auto* mockRaw = mock.get();
    StorageClient client(std::move(mock));

    QString uploadCid;
    QString downloadError;
    bool uploadCalled = false;
    bool downloadCalled = false;

    client.uploadFile(m_existingFile,
        [&](const QString& cid, const QString&) {
            uploadCid = cid;
            uploadCalled = true;
        });

    client.downloadToFile(QStringLiteral("zDvZDL"),
                          m_tmpDir.filePath("out.bin"),
        [&](const QString& err) {
            downloadError = err;
            downloadCalled = true;
        });

    QCOMPARE(mockRaw->uploadCalls.size(), 1);
    QCOMPARE(mockRaw->downloadCalls.size(), 1);
    QVERIFY(!uploadCalled);
    QVERIFY(!downloadCalled);

    mockRaw->fireUploadDone(QStringLiteral("zDvZUL"));
    mockRaw->fireDownloadDone();

    QVERIFY(uploadCalled);
    QVERIFY(downloadCalled);
    QCOMPARE(uploadCid, QStringLiteral("zDvZUL"));
    QVERIFY(downloadError.isEmpty());
}

// ── Destructor cleanup ──────────────────────────────────────────────────────

void TestStorageClient::testDestructorFailsPending()
{
    QString receivedError;
    bool called = false;

    {
        auto mock = std::make_unique<MockStorageTransport>();
        StorageClient client(std::move(mock));

        client.uploadFile(m_existingFile,
            [&](const QString& cid, const QString& err) {
                Q_UNUSED(cid);
                receivedError = err;
                called = true;
            });
        // No fireUploadDone — let the destructor clean up.
    }

    QVERIFY(called);
    QVERIFY(receivedError.contains(QStringLiteral("destroyed")));
}

QTEST_MAIN(TestStorageClient)
#include "test_storage_client.moc"
