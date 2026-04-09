#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <functional>
#include <memory>

class LogosAPIClient;

/**
 * StorageTransport — minimal abstraction over the storage_module IPC
 * surface. Allows StorageClient to be tested without linking the full
 * Logos SDK and without needing a running storage_module.
 *
 * The real implementation (LogosStorageTransport) wraps LogosAPIClient.
 * Tests provide their own implementation that records calls and fires
 * synthetic events.
 */
class StorageTransport
{
public:
    using EventCallback =
        std::function<void(const QString& eventName, const QVariantList& args)>;

    virtual ~StorageTransport() = default;

    /** True if the underlying client is connected to storage_module. */
    virtual bool isConnected() const = 0;

    /** Invoke StorageModulePlugin::uploadUrl(QUrl, int chunkSize). */
    virtual void uploadUrl(const QUrl& fileUrl, int chunkSize) = 0;

    /** Invoke StorageModulePlugin::downloadToUrl(cid, destUrl, local, chunkSize). */
    virtual void downloadToUrl(const QString& cid,
                               const QUrl& destUrl,
                               bool localOnly,
                               int chunkSize) = 0;

    /** Subscribe to the storage_module's eventResponse signal. */
    virtual void subscribeEventResponse(EventCallback cb) = 0;
};

/**
 * Real transport — forwards to a LogosAPIClient instance targeting
 * "storage_module". Does not own the client.
 */
class LogosStorageTransport : public StorageTransport
{
public:
    explicit LogosStorageTransport(LogosAPIClient* client);
    ~LogosStorageTransport() override = default;

    bool isConnected() const override;
    void uploadUrl(const QUrl& fileUrl, int chunkSize) override;
    void downloadToUrl(const QString& cid,
                       const QUrl& destUrl,
                       bool localOnly,
                       int chunkSize) override;
    void subscribeEventResponse(EventCallback cb) override;

private:
    LogosAPIClient* m_client;   // not owned
};

/**
 * StorageClient — high-level wrapper around the storage_module IPC
 * surface. Uses one-shot uploadUrl / downloadToUrl (not manual chunk
 * orchestration) — the storage module wraps libstorage.so's
 * storage_upload_file internally (verified by the UploadFileCallbackCtx
 * symbol in storage_module_plugin.so).
 *
 * Async results arrive via eventResponse. Known event names from symbol
 * inspection: storageUploadDone, storageDownloadDone, storageUploadProgress,
 * storageDownloadProgress. v2.0 ignores progress events.
 *
 * The exact shape of eventResponse args is NOT verified from symbols alone.
 * When storage_module is installed in LogosBasecamp, the shape must be
 * confirmed and this class may need adjustment. Extraction logic below
 * documents assumptions.
 */
class StorageClient : public QObject
{
    Q_OBJECT

public:
    using UploadCallback   = std::function<void(const QString& cidOrEmpty,
                                                const QString& errorOrEmpty)>;
    using DownloadCallback = std::function<void(const QString& errorOrEmpty)>;

    /**
     * @param transport  Transport to use. Takes ownership. Pass nullptr
     *                   (or a transport that returns isConnected()==false)
     *                   to simulate "storage_module not available".
     */
    explicit StorageClient(std::unique_ptr<StorageTransport> transport,
                           QObject* parent = nullptr);
    ~StorageClient() override;

    /** True if the transport reports it is connected to storage_module. */
    bool isAvailable() const;

    /**
     * Upload a local file. One-shot — storage module chunks internally.
     * Callback fires exactly once with either a CID (success) or an error
     * message.
     *
     * If the transport is unavailable, the callback fires synchronously
     * with an error.
     */
    void uploadFile(const QString& filePath, UploadCallback cb);

    /**
     * Download a blob by CID to a local file. Callback fires exactly once
     * with an error string (empty = success).
     */
    void downloadToFile(const QString& cid,
                        const QString& destPath,
                        DownloadCallback cb);

private:
    struct PendingUpload {
        QString        filePath;
        UploadCallback cb;
    };
    struct PendingDownload {
        QString          cid;
        QString          destPath;
        DownloadCallback cb;
    };

    void subscribeEventsIfNeeded();
    void onEventResponse(const QString& eventName, const QVariantList& args);
    void failAllPending(const QString& error);

    std::unique_ptr<StorageTransport> m_transport;
    QList<PendingUpload>              m_pendingUploads;
    QList<PendingDownload>            m_pendingDownloads;
    bool                              m_subscribed = false;

    static constexpr int kChunkSize = 64 * 1024;      // 64 KiB default

    // Events (from symbol inspection of storage_module_plugin.so)
    static constexpr const char* kEventUploadDone   = "storageUploadDone";
    static constexpr const char* kEventDownloadDone = "storageDownloadDone";
};
