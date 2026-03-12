#include "NotesPlugin.h"

NotesPlugin::NotesPlugin(QObject* parent)
    : QObject(parent)
{
}

// ── Shell lifecycle ──────────────────────────────────────────────────────────

void NotesPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;
}

QString NotesPlugin::initialize()
{
    // NotesBackend constructor already calls m_db.init() and sets the
    // initial screen. Nothing more to do here.
    return ok();
}

// ── Routing ──────────────────────────────────────────────────────────────────

QString NotesPlugin::isInitialized()
{
    // If the DB has a wrapped key the user is returning; otherwise first run.
    return (m_backend.currentScreen() == QStringLiteral("unlock"))
               ? QStringLiteral("true")
               : QStringLiteral("false");
}

// ── Account management ───────────────────────────────────────────────────────

QString NotesPlugin::importMnemonic(const QString& mnemonic,
                                    const QString& pin,
                                    const QString& confirm)
{
    m_backend.importMnemonic(mnemonic, pin, confirm);

    if (m_backend.currentScreen() == QStringLiteral("note"))
        return successJson();

    return errorJson(m_backend.errorMessage());
}

QString NotesPlugin::unlockWithPin(const QString& pin)
{
    m_backend.unlockWithPin(pin);

    if (m_backend.currentScreen() == QStringLiteral("note"))
        return successJson();

    return errorJson(m_backend.errorMessage());
}

// ── Note persistence ─────────────────────────────────────────────────────────

QString NotesPlugin::loadNote()
{
    return m_backend.loadNote();
}

QString NotesPlugin::saveNote(const QString& text)
{
    m_backend.saveNote(text);
    return ok();
}

// ── Helpers ──────────────────────────────────────────────────────────────────

QString NotesPlugin::ok()
{
    return QStringLiteral("ok");
}

QString NotesPlugin::successJson()
{
    return QStringLiteral("{\"success\":true}");
}

QString NotesPlugin::errorJson(const QString& msg)
{
    // Escape any double-quotes in the message to keep JSON valid.
    QString safe = msg;
    safe.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    safe.replace(QLatin1Char('"'),  QStringLiteral("\\\""));
    return QStringLiteral("{\"error\":\"") + safe + QStringLiteral("\"}");
}
