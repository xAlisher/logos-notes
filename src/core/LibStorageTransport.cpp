#include "LibStorageTransport.h"

#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUrl>

extern "C" {
#include "libstorage.h"
// Nim runtime initialiser exported by libstorage.a.
// Must be called exactly once before any storage_* function.
void libstorageNimMain(void);
}

// Build the config JSON at runtime so dataDir is always under the real
// user's home — never a hardcoded path.
static QByteArray buildConfig()
{
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/storage");
    QDir().mkpath(dataDir);
    return QStringLiteral("{\"dataDir\":\"%1\",\"logLevel\":\"WARN\"}")
        .arg(dataDir)
        .toUtf8();
}

// ── Construction / destruction ────────────────────────────────────────────────

LibStorageTransport::LibStorageTransport(QObject* parent)
    : QObject(parent)
{
    connect(this, &LibStorageTransport::_storageStarted,
            this, &LibStorageTransport::onStorageStarted,
            Qt::QueuedConnection);
    connect(this, &LibStorageTransport::_uploadInitDone,
            this, &LibStorageTransport::onUploadInitDone,
            Qt::QueuedConnection);
    connect(this, &LibStorageTransport::_uploadFileDone,
            this, &LibStorageTransport::onUploadFileDone,
            Qt::QueuedConnection);
    connect(this, &LibStorageTransport::_downloadDone,
            this, &LibStorageTransport::onDownloadDone,
            Qt::QueuedConnection);
}

LibStorageTransport::~LibStorageTransport()
{
    if (!m_ctx) return;
    // Best-effort shutdown — ignore callback results on teardown.
    storage_stop(m_ctx, nullptr, nullptr);
    storage_close(m_ctx, nullptr, nullptr);
    storage_destroy(m_ctx);
    m_ctx = nullptr;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void LibStorageTransport::start()
{
    if (m_ctx) return;  // Already started (or startup succeeded earlier).

    // Initialise the Nim runtime embedded in libstorage.a.
    // Safe to call multiple times — Nim's generated init guard is idempotent.
    libstorageNimMain();

    const QByteArray config = buildConfig();
    auto* ctx = new StartCtx{this};
    // storage_new is synchronous — returns the context immediately.
    m_ctx = storage_new(config.constData(), cbStart, ctx);
    if (!m_ctx) {
        qWarning() << "LibStorageTransport: storage_new failed";
        delete ctx;
        return;
    }

    // storage_start is async — cbStart fires when ready.
    int ret = storage_start(m_ctx, cbStart, ctx);
    if (ret != RET_OK) {
        qWarning() << "LibStorageTransport: storage_start returned" << ret;
        delete ctx;
        // Reset m_ctx so start() can be retried — otherwise m_ctx stays
        // non-null and the guard above would block all future attempts.
        storage_destroy(m_ctx);
        m_ctx = nullptr;
    }
}

// ── StorageTransport interface ────────────────────────────────────────────────

bool LibStorageTransport::isConnected() const
{
    return m_ready.load();
}

void LibStorageTransport::uploadUrl(const QUrl& fileUrl, int chunkSize)
{
    if (!m_ctx || !m_ready.load()) {
        qWarning() << "LibStorageTransport::uploadUrl: node not ready";
        fireEvent(QStringLiteral("storageUploadDone"), {});
        return;
    }

    const QString filepath = fileUrl.toLocalFile();
    if (filepath.isEmpty()) {
        qWarning() << "LibStorageTransport::uploadUrl: invalid file URL" << fileUrl;
        fireEvent(QStringLiteral("storageUploadDone"), {});
        return;
    }

    m_pendingUpload = {filepath, chunkSize, {}};

    auto* ctx = new UploadInitCtx{this, filepath, chunkSize};
    int ret = storage_upload_init(m_ctx,
                                  filepath.toUtf8().constData(),
                                  static_cast<size_t>(chunkSize),
                                  cbUploadInit,
                                  ctx);
    if (ret != RET_OK) {
        qWarning() << "LibStorageTransport::uploadUrl: storage_upload_init returned" << ret;
        delete ctx;
        fireEvent(QStringLiteral("storageUploadDone"), {});
    }
}

void LibStorageTransport::downloadToUrl(const QString& cid,
                                        const QUrl& destUrl,
                                        bool localOnly,
                                        int chunkSize)
{
    if (!m_ctx || !m_ready.load()) {
        qWarning() << "LibStorageTransport::downloadToUrl: node not ready";
        fireEvent(QStringLiteral("storageDownloadDone"),
                  {QStringLiteral("Storage node not ready")});
        return;
    }

    const QString destPath = destUrl.toLocalFile();
    m_pendingDownload = {cid, destPath, localOnly, chunkSize};

    auto* ctx = new DownloadCtx{this, cid, destPath, localOnly, chunkSize};
    int ret = storage_download_init(m_ctx,
                                    cid.toUtf8().constData(),
                                    static_cast<size_t>(chunkSize),
                                    localOnly,
                                    cbDownloadStream,
                                    ctx);
    if (ret != RET_OK) {
        qWarning() << "LibStorageTransport::downloadToUrl: storage_download_init returned" << ret;
        delete ctx;
        fireEvent(QStringLiteral("storageDownloadDone"),
                  {QStringLiteral("download_init failed")});
    }
}

void LibStorageTransport::subscribeEventResponse(EventCallback cb)
{
    m_eventCb = std::move(cb);
}

// ── Qt-thread slots (marshalled from Nim callbacks) ───────────────────────────

void LibStorageTransport::onStorageStarted(bool ok, QString error)
{
    if (ok) {
        m_ready.store(true);
        qDebug() << "LibStorageTransport: node ready";
    } else {
        qWarning() << "LibStorageTransport: node start failed:" << error;
    }
}

void LibStorageTransport::onUploadInitDone(bool ok, QString sessionId,
                                           QString filepath, int chunkSize)
{
    if (!ok || sessionId.isEmpty()) {
        qWarning() << "LibStorageTransport: upload_init failed";
        fireEvent(QStringLiteral("storageUploadDone"), {});
        return;
    }

    m_pendingUpload.sessionId = sessionId;

    auto* ctx = new UploadFileCtx{this};
    int ret = storage_upload_file(m_ctx,
                                  sessionId.toUtf8().constData(),
                                  cbUploadFile,
                                  ctx);
    if (ret != RET_OK) {
        qWarning() << "LibStorageTransport: storage_upload_file returned" << ret;
        delete ctx;
        fireEvent(QStringLiteral("storageUploadDone"), {});
    }
}

void LibStorageTransport::onUploadFileDone(bool ok, QString cid, QString error)
{
    if (!ok || cid.isEmpty()) {
        qWarning() << "LibStorageTransport: upload failed:" << error;
        fireEvent(QStringLiteral("storageUploadDone"), {});
    } else {
        qDebug() << "LibStorageTransport: upload done, CID:" << cid;
        fireEvent(QStringLiteral("storageUploadDone"), {cid});
    }
    m_pendingUpload = {};
}

void LibStorageTransport::onDownloadDone(bool ok, QString error)
{
    if (!ok) {
        qWarning() << "LibStorageTransport: download failed:" << error;
        fireEvent(QStringLiteral("storageDownloadDone"), {error});
    } else {
        qDebug() << "LibStorageTransport: download done";
        fireEvent(QStringLiteral("storageDownloadDone"), {});
    }
    m_pendingDownload = {};
}

// ── Private helpers ───────────────────────────────────────────────────────────

void LibStorageTransport::fireEvent(const QString& eventName, const QVariantList& args)
{
    if (m_eventCb)
        m_eventCb(eventName, args);
}

// ── Static C callbacks (Nim threads) ─────────────────────────────────────────

void LibStorageTransport::cbStart(int ret, const char* msg, size_t /*len*/, void* ud)
{
    auto* ctx = static_cast<StartCtx*>(ud);
    const bool ok = (ret == RET_OK);
    const QString error = (msg && !ok) ? QString::fromUtf8(msg) : QString();
    emit ctx->self->_storageStarted(ok, error);
    if (ret != RET_PROGRESS)
        delete ctx;
}

void LibStorageTransport::cbUploadInit(int ret, const char* msg, size_t /*len*/, void* ud)
{
    auto* ctx = static_cast<UploadInitCtx*>(ud);
    const bool ok = (ret == RET_OK);
    const QString sessionId = (msg && ok) ? QString::fromUtf8(msg) : QString();
    emit ctx->self->_uploadInitDone(ok, sessionId, ctx->filepath, ctx->chunkSize);
    if (ret != RET_PROGRESS)
        delete ctx;
}

void LibStorageTransport::cbUploadFile(int ret, const char* msg, size_t /*len*/, void* ud)
{
    auto* ctx = static_cast<UploadFileCtx*>(ud);
    if (ret == RET_PROGRESS) return;  // progress update — ignore
    const bool ok = (ret == RET_OK);
    const QString cid   = (msg && ok) ? QString::fromUtf8(msg) : QString();
    const QString error = (msg && !ok) ? QString::fromUtf8(msg) : QString();
    emit ctx->self->_uploadFileDone(ok, cid, error);
    delete ctx;
}

void LibStorageTransport::cbDownloadStream(int ret, const char* msg, size_t /*len*/, void* ud)
{
    auto* ctx = static_cast<DownloadCtx*>(ud);
    if (ret == RET_PROGRESS) {
        // Progress: if node is downloading, kick off streaming if needed.
        // Phase 1: fire the actual stream call once init callback arrives.
        const QString sessionInfo = msg ? QString::fromUtf8(msg) : QString();
        // After download_init succeeds (first callback), start the stream.
        int streamRet = storage_download_stream(
            ctx->self->m_ctx,
            ctx->cid.toUtf8().constData(),
            static_cast<size_t>(ctx->chunkSize),
            ctx->localOnly,
            ctx->destPath.toUtf8().constData(),
            cbDownloadStream,
            ud);  // same ctx, same callback — next call will be RET_OK or RET_ERR
        if (streamRet != RET_OK) {
            qWarning() << "LibStorageTransport: storage_download_stream returned" << streamRet;
            emit ctx->self->_downloadDone(false, QStringLiteral("download_stream failed"));
            delete ctx;
        }
        return;
    }
    const bool ok = (ret == RET_OK);
    const QString error = (msg && !ok) ? QString::fromUtf8(msg) : QString();
    emit ctx->self->_downloadDone(ok, error);
    delete ctx;
}
