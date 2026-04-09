#include "StorageClient.h"

#include <QDebug>
#include <QFileInfo>
#include <QVariant>

// StorageClient — transport-agnostic core. The real LogosAPIClient-backed
// transport lives in LogosStorageTransport.cpp and is only linked into the
// plugin build, not tests.

StorageClient::StorageClient(std::unique_ptr<StorageTransport> transport,
                             QObject* parent)
    : QObject(parent)
    , m_transport(std::move(transport))
{
}

StorageClient::~StorageClient()
{
    // Any still-pending requests will never complete — surface to callers
    // so their callbacks don't silently leak.
    if (m_pendingUpload)
        failPendingUpload(QString(),
                          QStringLiteral("StorageClient destroyed with pending request"));
    if (m_pendingDownload)
        failPendingDownload(QStringLiteral("StorageClient destroyed with pending request"));
}

bool StorageClient::isAvailable() const
{
    return m_transport && m_transport->isConnected();
}

void StorageClient::setTimeoutMs(int ms)
{
    m_timeoutMs = ms;
}

// ── uploadFile ──────────────────────────────────────────────────────────────

void StorageClient::uploadFile(const QString& filePath, UploadCallback cb)
{
    if (!cb) {
        qWarning() << "StorageClient::uploadFile called with null callback";
        return;
    }

    if (!isAvailable()) {
        cb(QString(), QStringLiteral("storage_module not available"));
        return;
    }

    // Single-in-flight contract: reject overlap synchronously.
    if (m_pendingUpload) {
        cb(QString(), QStringLiteral("storage_module busy: upload already in progress"));
        return;
    }

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        cb(QString(), QStringLiteral("File does not exist: %1").arg(filePath));
        return;
    }

    subscribeEventsIfNeeded();

    // Build the pending entry BEFORE invoking so a synchronous response
    // (possible in mocks) still finds its slot.
    PendingUpload pu;
    pu.filePath = fi.absoluteFilePath();
    pu.cb       = std::move(cb);
    if (m_timeoutMs > 0) {
        pu.timer = std::make_unique<QTimer>();
        pu.timer->setSingleShot(true);
        pu.timer->setInterval(m_timeoutMs);
        connect(pu.timer.get(), &QTimer::timeout,
                this, &StorageClient::onUploadTimeout);
        pu.timer->start();
    }
    m_pendingUpload = std::move(pu);

    m_transport->uploadUrl(QUrl::fromLocalFile(fi.absoluteFilePath()),
                           kChunkSize);
}

// ── downloadToFile ──────────────────────────────────────────────────────────

void StorageClient::downloadToFile(const QString& cid,
                                   const QString& destPath,
                                   DownloadCallback cb)
{
    if (!cb) {
        qWarning() << "StorageClient::downloadToFile called with null callback";
        return;
    }

    if (!isAvailable()) {
        cb(QStringLiteral("storage_module not available"));
        return;
    }

    if (m_pendingDownload) {
        cb(QStringLiteral("storage_module busy: download already in progress"));
        return;
    }

    if (cid.isEmpty()) {
        cb(QStringLiteral("CID is empty"));
        return;
    }

    if (destPath.isEmpty()) {
        cb(QStringLiteral("Destination path is empty"));
        return;
    }

    subscribeEventsIfNeeded();

    PendingDownload pd;
    pd.cid      = cid;
    pd.destPath = destPath;
    pd.cb       = std::move(cb);
    if (m_timeoutMs > 0) {
        pd.timer = std::make_unique<QTimer>();
        pd.timer->setSingleShot(true);
        pd.timer->setInterval(m_timeoutMs);
        connect(pd.timer.get(), &QTimer::timeout,
                this, &StorageClient::onDownloadTimeout);
        pd.timer->start();
    }
    m_pendingDownload = std::move(pd);

    m_transport->downloadToUrl(cid,
                               QUrl::fromLocalFile(destPath),
                               /*localOnly=*/true,
                               kChunkSize);
}

// ── Event handling ──────────────────────────────────────────────────────────

void StorageClient::subscribeEventsIfNeeded()
{
    if (m_subscribed || !m_transport)
        return;

    m_transport->subscribeEventResponse(
        [this](const QString& name, const QVariantList& args) {
            this->onEventResponse(name, args);
        });

    m_subscribed = true;
}

void StorageClient::onEventResponse(const QString& eventName,
                                    const QVariantList& args)
{
    // NOTE: The exact shape of args is not verified from symbol inspection.
    // Assumed layouts (to confirm against a real storage_module):
    //   storageUploadDone:   args contain the CID as a non-empty QString
    //                        (taken as the last such entry).
    //   storageDownloadDone: args may contain an error QString distinct
    //                        from the CID; empty or CID-only = success.
    //
    // Single-in-flight: if we receive a *Done event with no pending
    // request of that type, the event is stray (e.g., from a different
    // consumer) and is ignored.

    if (eventName == QLatin1String(kEventUploadDone)) {
        if (!m_pendingUpload)
            return;  // Stray event — not ours.

        // Extract CID: take the last non-empty string-convertible QVariant.
        QString cid;
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            if (it->canConvert<QString>()) {
                const QString s = it->toString();
                if (!s.isEmpty()) { cid = s; break; }
            }
        }

        if (cid.isEmpty())
            failPendingUpload(QString(),
                              QStringLiteral("Upload succeeded but no CID in response"));
        else
            failPendingUpload(cid, QString());
        return;
    }

    if (eventName == QLatin1String(kEventDownloadDone)) {
        if (!m_pendingDownload)
            return;

        const QString pendingCid = m_pendingDownload->cid;

        // Scan args for an error string distinct from the CID.
        QString error;
        for (const auto& v : args) {
            if (!v.canConvert<QString>()) continue;
            const QString s = v.toString();
            if (s.isEmpty() || s == pendingCid) continue;
            error = s;
            break;
        }

        failPendingDownload(error);
        return;
    }

    // Other events (progress, lifecycle) are ignored in v2.0.
}

void StorageClient::onUploadTimeout()
{
    if (!m_pendingUpload)
        return;
    failPendingUpload(QString(),
                      QStringLiteral("Upload timed out after %1 ms").arg(m_timeoutMs));
}

void StorageClient::onDownloadTimeout()
{
    if (!m_pendingDownload)
        return;
    failPendingDownload(QStringLiteral("Download timed out after %1 ms").arg(m_timeoutMs));
}

void StorageClient::failPendingUpload(const QString& cidOrEmpty,
                                      const QString& error)
{
    if (!m_pendingUpload)
        return;
    // Move out so the slot is free before invoking the callback (which
    // may synchronously kick off a new request).
    PendingUpload p = std::move(*m_pendingUpload);
    m_pendingUpload.reset();
    if (p.timer)
        p.timer->stop();
    if (p.cb)
        p.cb(cidOrEmpty, error);
}

void StorageClient::failPendingDownload(const QString& error)
{
    if (!m_pendingDownload)
        return;
    PendingDownload p = std::move(*m_pendingDownload);
    m_pendingDownload.reset();
    if (p.timer)
        p.timer->stop();
    if (p.cb)
        p.cb(error);
}
