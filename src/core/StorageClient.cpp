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
    failAllPending(QStringLiteral("StorageClient destroyed with pending requests"));
}

bool StorageClient::isAvailable() const
{
    return m_transport && m_transport->isConnected();
}

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

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        cb(QString(), QStringLiteral("File does not exist: %1").arg(filePath));
        return;
    }

    subscribeEventsIfNeeded();

    // Queue BEFORE invoking so a synchronous response (possible in mocks)
    // still finds its entry.
    m_pendingUploads.append({fi.absoluteFilePath(), std::move(cb)});

    m_transport->uploadUrl(QUrl::fromLocalFile(fi.absoluteFilePath()),
                           kChunkSize);
}

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

    if (cid.isEmpty()) {
        cb(QStringLiteral("CID is empty"));
        return;
    }

    if (destPath.isEmpty()) {
        cb(QStringLiteral("Destination path is empty"));
        return;
    }

    subscribeEventsIfNeeded();

    m_pendingDownloads.append({cid, destPath, std::move(cb)});

    m_transport->downloadToUrl(cid,
                               QUrl::fromLocalFile(destPath),
                               /*localOnly=*/true,
                               kChunkSize);
}

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

    if (eventName == QLatin1String(kEventUploadDone)) {
        if (m_pendingUploads.isEmpty())
            return;  // Not our event.

        const PendingUpload req = m_pendingUploads.takeFirst();

        QString cid;
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            if (it->canConvert<QString>()) {
                const QString s = it->toString();
                if (!s.isEmpty()) { cid = s; break; }
            }
        }

        if (cid.isEmpty())
            req.cb(QString(),
                   QStringLiteral("Upload succeeded but no CID in response"));
        else
            req.cb(cid, QString());
        return;
    }

    if (eventName == QLatin1String(kEventDownloadDone)) {
        if (m_pendingDownloads.isEmpty())
            return;

        const PendingDownload req = m_pendingDownloads.takeFirst();

        QString error;
        for (const auto& v : args) {
            if (!v.canConvert<QString>()) continue;
            const QString s = v.toString();
            if (s.isEmpty() || s == req.cid) continue;
            error = s;
            break;
        }

        req.cb(error);
        return;
    }

    // Other events (progress, lifecycle) are ignored in v2.0.
}

void StorageClient::failAllPending(const QString& error)
{
    for (auto& p : m_pendingUploads)
        if (p.cb) p.cb(QString(), error);
    m_pendingUploads.clear();

    for (auto& p : m_pendingDownloads)
        if (p.cb) p.cb(error);
    m_pendingDownloads.clear();
}
