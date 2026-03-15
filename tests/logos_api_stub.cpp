// Minimal stub for LogosAPI — provides staticMetaObject and vtable
// needed by MOC for the Q_INVOKABLE initLogos(LogosAPI*) in NotesPlugin.
// The test never calls initLogos, so no real implementation is needed.

#include "logos_api.h"

LogosAPI::LogosAPI(const QString& /*module_name*/, QObject* parent)
    : QObject(parent), m_provider(nullptr), m_token_manager(nullptr) {}

LogosAPI::~LogosAPI() {}

LogosAPIProvider* LogosAPI::getProvider() const { return nullptr; }
LogosAPIClient* LogosAPI::getClient(const QString&) const { return nullptr; }
TokenManager* LogosAPI::getTokenManager() const { return nullptr; }
