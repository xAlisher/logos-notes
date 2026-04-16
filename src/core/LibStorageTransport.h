#pragma once

#include "StorageClient.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <atomic>
#include <mutex>
#include <string>

/**
 * LibStorageTransport — StorageTransport implementation backed by the
 * logos-storage-nim static library (libstorage.a).
 *
 * Embeds a Logos Storage node in-process. No Logos IPC, no capability
 * tokens, no separate process. Callbacks from libstorage run on Nim
 * threads and are marshalled back to the Qt main thread via invokeMethod.
 *
 * Lifecycle:
 *   LibStorageTransport t;
 *   t.start();   // async — ready when isConnected() returns true
 *   // use uploadUrl / downloadToUrl
 *   // destroyed with the owning StorageClient
 *
 * Thread safety: all public methods must be called from the Qt main
 * thread. Callbacks arrive on Nim threads and post back to main thread.
 */
class LibStorageTransport : public QObject, public StorageTransport
{
    Q_OBJECT

public:
    explicit LibStorageTransport(QObject* parent = nullptr);
    ~LibStorageTransport() override;

    // Start the embedded storage node. Safe to call before start() returns;
    // isConnected() will return true once the node is ready.
    void start();

    // ── StorageTransport interface ──────────────────────────────────────

    bool isConnected() const override;

    // Initiates an async upload. On completion fires subscribed callbacks
    // with event "storageUploadDone", args=[cid] on success or args=[] on
    // failure.
    void uploadUrl(const QUrl& fileUrl, int chunkSize) override;

    // Initiates an async download. On completion fires subscribed callbacks
    // with event "storageDownloadDone", args=[] on success or args=[error]
    // on failure.
    void downloadToUrl(const QString& cid,
                       const QUrl& destUrl,
                       bool localOnly,
                       int chunkSize) override;

    void subscribeEventResponse(EventCallback cb) override;

private slots:
    // Called on the Qt main thread after a libstorage callback fires.
    void onStorageStarted(bool ok, QString error);
    void onUploadInitDone(bool ok, QString sessionId, QString filepath, int chunkSize);
    void onUploadFileDone(bool ok, QString cid, QString error);
    void onDownloadDone(bool ok, QString error);

signals:
    // Internal signals used to marshal Nim-thread callbacks → Qt main thread.
    void _storageStarted(bool ok, QString error);
    void _uploadInitDone(bool ok, QString sessionId, QString filepath, int chunkSize);
    void _uploadFileDone(bool ok, QString cid, QString error);
    void _downloadDone(bool ok, QString error);

private:
    void fireEvent(const QString& eventName, const QVariantList& args);

    void* m_ctx = nullptr;
    std::atomic<bool> m_ready{false};

    // Guards m_eventCallbacks — written once from Qt thread, read from Qt thread
    // only (via invokeMethod), but stored atomically to be safe.
    EventCallback m_eventCb;

    // Pending upload state (single-in-flight, enforced by StorageClient).
    struct UploadState {
        QString filepath;
        int chunkSize = 0;
        QString sessionId;
    };
    UploadState m_pendingUpload;

    // Pending download state.
    struct DownloadState {
        QString cid;
        QString destPath;
        bool localOnly = false;
        int chunkSize = 0;
    };
    DownloadState m_pendingDownload;

    // ── Static C callbacks (called on Nim threads) ─────────────────────

    struct StartCtx   { LibStorageTransport* self; };
    struct UploadInitCtx { LibStorageTransport* self; QString filepath; int chunkSize; };
    struct UploadFileCtx { LibStorageTransport* self; };
    struct DownloadCtx   { LibStorageTransport* self; QString cid; QString destPath;
                           bool localOnly; int chunkSize; };

    static void cbStart(int ret, const char* msg, size_t len, void* ud);
    static void cbUploadInit(int ret, const char* msg, size_t len, void* ud);
    static void cbUploadFile(int ret, const char* msg, size_t len, void* ud);
    static void cbDownloadStream(int ret, const char* msg, size_t len, void* ud);
};
