// test_lib_storage_transport.cpp — unit tests for LibStorageTransport.
//
// Scope: tests that exercise LibStorageTransport's interface behaviour
// WITHOUT starting a real storage node.  These cover the "not ready" guard
// paths (uploadUrl / downloadToUrl before start()), subscription wiring,
// and isConnected() state transitions driven by the internal signal.
//
// Integration tests (real node startup + upload/download round-trip)
// are intentionally omitted from CI — starting a Nim P2P node takes
// several seconds and requires a writeable data directory.  Run them
// manually via the test_lib_storage_integration target (out of scope here).
//
// NOTE: libstorageNimMain is NOT called here — tests that never invoke
// storage_new() do not need the Nim runtime, so startup cost is zero.

#include <QtTest/QtTest>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include "core/LibStorageTransport.h"

// ── Helper ────────────────────────────────────────────────────────────────────

// Drives the Qt event loop for up to `maxMs` milliseconds, or until
// `done` becomes true.  Returns true if `done` was set before the timeout.
static bool waitFor(bool& done, int maxMs = 1000)
{
    QEventLoop loop;
    QTimer t;
    t.setSingleShot(true);
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(maxMs);
    while (!done && t.isActive())
        loop.processEvents(QEventLoop::AllEvents, 10);
    return done;
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestLibStorageTransport : public QObject
{
    Q_OBJECT

private slots:
    void testIsConnectedFalseBeforeStart();
    void testUploadWhenNotReadyFiresEmptyDoneEvent();
    void testDownloadWhenNotReadyFiresErrorEvent();
    void testSubscribeEventResponseReceivesUploadDone();
    void testSubscribeEventResponseReceivesDownloadDone();
    void testUploadWithEmptyUrlFiresError();
};

// ── isConnected ───────────────────────────────────────────────────────────────

void TestLibStorageTransport::testIsConnectedFalseBeforeStart()
{
    LibStorageTransport t;
    // start() is NOT called — node was never started
    QVERIFY(!t.isConnected());
}

// ── uploadUrl before node is ready ────────────────────────────────────────────

void TestLibStorageTransport::testUploadWhenNotReadyFiresEmptyDoneEvent()
{
    LibStorageTransport t;

    QString receivedEvent;
    QVariantList receivedArgs;
    bool called = false;

    t.subscribeEventResponse([&](const QString& ev, const QVariantList& args) {
        receivedEvent = ev;
        receivedArgs  = args;
        called        = true;
    });

    // Node not started → upload guard must fire storageUploadDone with empty args.
    t.uploadUrl(QUrl::fromLocalFile(QStringLiteral("/tmp/nonexistent.bin")), 65536);

    // The event is emitted directly (synchronous path in the guard).
    QVERIFY(called);
    QCOMPARE(receivedEvent, QStringLiteral("storageUploadDone"));
    QVERIFY(receivedArgs.isEmpty());  // no CID on failure
}

// ── downloadToUrl before node is ready ───────────────────────────────────────

void TestLibStorageTransport::testDownloadWhenNotReadyFiresErrorEvent()
{
    LibStorageTransport t;

    QString receivedEvent;
    QVariantList receivedArgs;
    bool called = false;

    t.subscribeEventResponse([&](const QString& ev, const QVariantList& args) {
        receivedEvent = ev;
        receivedArgs  = args;
        called        = true;
    });

    t.downloadToUrl(QStringLiteral("zDvZFakeCid"),
                    QUrl::fromLocalFile(QStringLiteral("/tmp/dest.bin")),
                    /*localOnly=*/true,
                    65536);

    QVERIFY(called);
    QCOMPARE(receivedEvent, QStringLiteral("storageDownloadDone"));
    // Error path: args should be non-empty (contains error string).
    QVERIFY(!receivedArgs.isEmpty());
    QVERIFY(!receivedArgs.first().toString().isEmpty());
}

// ── subscribeEventResponse wiring ─────────────────────────────────────────────

void TestLibStorageTransport::testSubscribeEventResponseReceivesUploadDone()
{
    LibStorageTransport t;

    int callCount = 0;
    t.subscribeEventResponse([&](const QString&, const QVariantList&) {
        ++callCount;
    });

    // Trigger twice — each uploadUrl when not connected fires one event.
    t.uploadUrl(QUrl::fromLocalFile(QStringLiteral("/a")), 65536);
    t.uploadUrl(QUrl::fromLocalFile(QStringLiteral("/b")), 65536);

    QCOMPARE(callCount, 2);
}

void TestLibStorageTransport::testSubscribeEventResponseReceivesDownloadDone()
{
    LibStorageTransport t;

    int callCount = 0;
    t.subscribeEventResponse([&](const QString&, const QVariantList&) {
        ++callCount;
    });

    t.downloadToUrl(QStringLiteral("cid1"),
                    QUrl::fromLocalFile(QStringLiteral("/d1")),
                    true, 65536);
    t.downloadToUrl(QStringLiteral("cid2"),
                    QUrl::fromLocalFile(QStringLiteral("/d2")),
                    true, 65536);

    QCOMPARE(callCount, 2);
}

// ── uploadUrl with invalid (non-local) URL ─────────────────────────────────────

void TestLibStorageTransport::testUploadWithEmptyUrlFiresError()
{
    LibStorageTransport t;

    bool called = false;
    t.subscribeEventResponse([&](const QString& ev, const QVariantList& args) {
        QCOMPARE(ev, QStringLiteral("storageUploadDone"));
        QVERIFY(args.isEmpty());
        called = true;
    });

    // A QUrl that is NOT a local file → toLocalFile() returns "".
    // The transport must detect this and fire storageUploadDone with no CID.
    t.uploadUrl(QUrl(QStringLiteral("https://example.com/file.bin")), 65536);

    QVERIFY(called);
}

QTEST_MAIN(TestLibStorageTransport)
#include "test_lib_storage_transport.moc"
