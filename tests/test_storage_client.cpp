#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>
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
};

class TestStorageClient : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testIsAvailableWhenTransportConnected();
    void testIsAvailableWhenTransportDisconnected();
    void testIsAvailableWhenTransportNull();

    void testUploadFileSuccess();
    void testUploadFileWithMissingFile();
    void testUploadFileWhenUnavailable();
    void testUploadFileCallsTransport();

    void testDownloadSuccess();
    void testDownloadWithEmptyCid();
    void testDownloadWithEmptyPath();
    void testDownloadWhenUnavailable();
    void testDownloadFailureFromEvent();

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

// ── uploadFile ──────────────────────────────────────────────────────────────

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

    // Simulate storage_module replying with a CID.
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

    QVERIFY(called);  // synchronous failure
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

    QVERIFY(called);  // synchronous failure
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

// ── downloadToFile ──────────────────────────────────────────────────────────

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

    // Simulate the storage module finishing the download (no error).
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
