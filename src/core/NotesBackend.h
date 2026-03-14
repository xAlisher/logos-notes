#pragma once

#include <QObject>
#include <QString>

#include "CryptoManager.h"
#include "DatabaseManager.h"
#include "KeyManager.h"

// QML-facing backend. Registered as a context property "backend".
// Drives screen navigation and note persistence.
class NotesBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentScreen READ currentScreen NOTIFY currentScreenChanged)
    Q_PROPERTY(QString errorMessage  READ errorMessage  NOTIFY errorMessageChanged)

public:
    explicit NotesBackend(QObject *parent = nullptr);

    QString currentScreen() const;
    QString errorMessage()  const;

    // Called from ImportScreen: validate mnemonic + PIN, derive key, save state.
    Q_INVOKABLE void importMnemonic(const QString &mnemonic,
                                    const QString &pin,
                                    const QString &pinConfirm,
                                    const QString &backupPath = {});

    // Called from UnlockScreen: re-derive key with PIN.
    Q_INVOKABLE void unlockWithPin(const QString &pin);

    // Called from NoteScreen: encrypt and persist the note (Phase 0 compat, id=1).
    Q_INVOKABLE void saveNote(const QString &plaintext);

    // Called from NoteScreen on load: decrypt and return the note (Phase 0 compat, id=1).
    Q_INVOKABLE QString loadNote();

    // ── Phase 1: multi-note API ─────────────────────────────────────────
    Q_INVOKABLE QString createNote();
    Q_INVOKABLE QString loadNotes();
    Q_INVOKABLE QString loadNote(int id);
    Q_INVOKABLE QString saveNote(int id, const QString &plaintext);
    Q_INVOKABLE QString deleteNote(int id);

    // Lock session: wipe in-memory key, go back to unlock screen.
    Q_INVOKABLE void lock();

    // Short hex fingerprint derived from master key (for display in Settings).
    Q_INVOKABLE QString getAccountFingerprint() const;

    // Returns true if the database has a stored account (wrapped key).
    bool hasAccount() const;

    // Export all notes as an encrypted backup file. Returns the file path or error.
    Q_INVOKABLE QString exportBackup(const QString &filePath);

    // Import notes from an encrypted backup file.
    // If mnemonic is provided, re-derives key using backup's salt to decrypt.
    // If empty, tries current master key (same-device restore).
    Q_INVOKABLE QString importBackup(const QString &filePath,
                                      const QString &mnemonic = {});

    // Wipe the database and return to the import screen.
    Q_INVOKABLE void resetAndWipe();

signals:
    void currentScreenChanged();
    void errorMessageChanged();

private:
    void setScreen(const QString &screen);
    void setError(const QString &msg);

    CryptoManager  m_crypto;
    DatabaseManager m_db;
    KeyManager      m_keys;

    QString m_currentScreen;
    QString m_errorMessage;

    void migratePlaintextTitles();

    // PIN brute-force protection (Issue #2)
    int  m_failedAttempts = 0;
    qint64 m_lockoutUntil = 0; // epoch seconds; 0 = no lockout
};
