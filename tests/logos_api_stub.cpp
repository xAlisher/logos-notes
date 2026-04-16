// Minimal stub for LogosAPI + LogosAPIClient — provides staticMetaObject
// and vtable needed by MOC for the Q_INVOKABLE initLogos(LogosAPI*) in
// NotesPlugin, and link-time stubs for LogosStorageTransport methods.
// Tests never actually call initLogos with a real API, so no real
// implementation is needed — getClient() returns nullptr, so
// LogosStorageTransport is never instantiated.

#include "logos_api.h"
#include "cpp/logos_api_client.h"

// ── LogosAPI stubs ────────────────────────────────────────────────────────────

LogosAPI::LogosAPI(const QString& /*module_name*/, QObject* parent)
    : QObject(parent), m_provider(nullptr), m_token_manager(nullptr) {}

LogosAPI::~LogosAPI() {}

LogosAPIProvider* LogosAPI::getProvider() const { return nullptr; }
LogosAPIClient* LogosAPI::getClient(const QString&) const { return nullptr; }
TokenManager* LogosAPI::getTokenManager() const { return nullptr; }

// ── LogosAPIClient stubs (required by LogosStorageTransport linkage) ──────────
// These are never called — getClient() returns nullptr so LogosStorageTransport
// is never constructed in test builds.

bool LogosAPIClient::isConnected() const { return false; }

QVariant LogosAPIClient::invokeRemoteMethod(
    const QString&, const QString&, const QVariant&, const QVariant&, Timeout)
{ return {}; }

QVariant LogosAPIClient::invokeRemoteMethod(
    const QString&, const QString&, const QVariant&, const QVariant&,
    const QVariant&, const QVariant&, Timeout)
{ return {}; }

void LogosAPIClient::onEvent(
    QObject*, QObject*, const QString&,
    std::function<void(const QString&, const QVariantList&)>)
{}
