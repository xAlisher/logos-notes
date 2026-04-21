#include "StorageClient.h"

#include "cpp/logos_api_client.h"

#include <QVariant>

// LogosStorageTransport — real implementation over LogosAPIClient.
// Kept in a separate TU from StorageClient.cpp so that tests can link
// StorageClient without pulling in the logos SDK.

namespace {
constexpr const char* kStorageObject = "StorageModulePlugin";
}

LogosStorageTransport::LogosStorageTransport(LogosAPIClient* client)
    : m_client(client)
{
}

bool LogosStorageTransport::isConnected() const
{
    return m_client && m_client->isConnected();
}

void LogosStorageTransport::uploadUrl(const QUrl& fileUrl, int chunkSize)
{
    if (!m_client)
        return;
    m_client->invokeRemoteMethod(
        QString::fromLatin1(kStorageObject),
        QStringLiteral("uploadUrl"),
        QVariant::fromValue(fileUrl),
        QVariant(chunkSize));
}

void LogosStorageTransport::downloadToUrl(const QString& cid,
                                          const QUrl& destUrl,
                                          bool localOnly,
                                          int chunkSize)
{
    if (!m_client)
        return;
    m_client->invokeRemoteMethod(
        QString::fromLatin1(kStorageObject),
        QStringLiteral("downloadToUrl"),
        QVariant(cid),
        QVariant::fromValue(destUrl),
        QVariant(localOnly),
        QVariant(chunkSize));
}

void LogosStorageTransport::subscribeEventResponse(EventCallback cb)
{
    if (!m_client)
        return;
    m_client->onEvent(
        nullptr,                    // origin: any
        QStringLiteral("eventResponse"),
        [cb = std::move(cb)](const QString& name, const QVariantList& args) {
            if (cb) cb(name, args);
        });
}
