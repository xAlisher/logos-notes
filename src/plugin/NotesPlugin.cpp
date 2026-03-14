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
    // Check the database directly — currentScreen may be "note" after import,
    // but on UI reload we need to know if the account exists.
    return m_backend.hasAccount()
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

// ── Phase 1: multi-note CRUD ────────────────────────────────────────────────

QString NotesPlugin::createNote()
{
    return m_backend.createNote();
}

QString NotesPlugin::loadNotes()
{
    return m_backend.loadNotes();
}

QString NotesPlugin::loadNote(int id)
{
    return m_backend.loadNote(id);
}

QString NotesPlugin::saveNote(int id, const QString& text)
{
    return m_backend.saveNote(id, text);
}

QString NotesPlugin::deleteNote(int id)
{
    return m_backend.deleteNote(id);
}

// ── Session management ──────────────────────────────────────────────────────

QString NotesPlugin::lockSession()
{
    m_backend.lock();
    return ok();
}

QString NotesPlugin::getAccountFingerprint()
{
    return m_backend.getAccountFingerprint();
}

QString NotesPlugin::resetAndWipe()
{
    m_backend.resetAndWipe();
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
