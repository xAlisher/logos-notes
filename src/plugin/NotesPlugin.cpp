#include "NotesPlugin.h"

#include "core/StorageClient.h"
#include "cpp/logos_api.h"

NotesPlugin::NotesPlugin(QObject* parent)
    : QObject(parent)
{
}

// ── Shell lifecycle ──────────────────────────────────────────────────────────

void NotesPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;
    // libstorage.a removed — Nim runtime (cmdLine/cmdCount) conflicts with
    // logos_host child process dlopen. Storage backup unavailable until
    // a token-free IPC path is available (upstream bug #141).
}

void NotesPlugin::ensureStorageClient()
{
    // No-op: storage client not available (libstorage.a removed, see initLogos).
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
                                    const QString& confirm,
                                    const QString& backupPath)
{
    m_backend.importMnemonic(mnemonic, pin, confirm, backupPath);

    if (m_backend.currentScreen() == QStringLiteral("note")) {
        // Surface partial restore warning if any.
        QString warning = m_backend.errorMessage();
        if (!warning.isEmpty()) {
            return QStringLiteral("{\"success\":true,\"warning\":\"")
                   + warning.replace('"', '\'') + QStringLiteral("\"}");
        }
        return successJson();
    }

    return errorJson(m_backend.errorMessage());
}

QString NotesPlugin::unlockWithPin(const QString& pin)
{
    m_backend.unlockWithPin(pin);

    if (m_backend.currentScreen() == QStringLiteral("note"))
        return successJson();

    return errorJson(m_backend.errorMessage());
}

// ── Note CRUD ───────────────────────────────────────────────────────────────

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

QString NotesPlugin::getKeySource()
{
    return m_backend.getKeySource();
}

QString NotesPlugin::getAccountFingerprint()
{
    return m_backend.getAccountFingerprint();
}

QString NotesPlugin::exportBackup(const QString& filePath)
{
    return m_backend.exportBackup(filePath);
}

QString NotesPlugin::exportBackupAuto()
{
    return m_backend.exportBackupAuto();
}

QString NotesPlugin::listBackups()
{
    return m_backend.listBackups();
}

QString NotesPlugin::importBackup(const QString& filePath,
                                   const QString& mnemonic)
{
    return m_backend.importBackup(filePath, mnemonic);
}

QString NotesPlugin::resetAndWipe()
{
    m_backend.resetAndWipe();
    return ok();
}

// ── Keycard Module Integration ───────────────────────────────────────────────

QString NotesPlugin::importWithKeycardKey(const QString& hexKey,
                                           const QString& backupPath)
{
    m_backend.importWithKeycardKey(hexKey, backupPath);

    if (m_backend.currentScreen() == QStringLiteral("note")) {
        QString warning = m_backend.errorMessage();
        if (!warning.isEmpty()) {
            return QStringLiteral("{\"success\":true,\"warning\":\"")
                   + warning.replace('"', '\'') + QStringLiteral("\"}");
        }
        return successJson();
    }

    return errorJson(m_backend.errorMessage());
}

QString NotesPlugin::unlockWithKeycardKey(const QString& hexKey)
{
    m_backend.unlockWithKeycardKey(hexKey);

    if (m_backend.currentScreen() == QStringLiteral("note"))
        return successJson();

    return errorJson(m_backend.errorMessage());
}

// ── Storage auto-backup (issue #72) ─────────────────────────────────────────

QString NotesPlugin::getBackupCid()
{
    ensureStorageClient();
    return m_backend.getBackupCid();
}

QString NotesPlugin::setBackupCid(const QString& cid, const QString& timestamp)
{
    return m_backend.setBackupCid(cid, timestamp);
}

QString NotesPlugin::getFileForStash()
{
    return m_backend.getFileForStash();
}

QString NotesPlugin::getStorageStatus()
{
    ensureStorageClient();
    return m_backend.getStorageStatus();
}

QString NotesPlugin::triggerBackup()
{
    ensureStorageClient();
    return m_backend.triggerBackup();
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
