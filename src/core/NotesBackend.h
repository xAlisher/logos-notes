#pragma once

#include <QObject>
#include <QString>

#include "CryptoManager.h"
#include "DatabaseManager.h"
#include "KeyManager.h"
#include "KeycardBridge.h"

// QML-facing backend. Registered as a context property "backend".
// Drives screen navigation and note persistence.
class NotesBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentScreen READ currentScreen NOTIFY currentScreenChanged)
    Q_PROPERTY(QString errorMessage  READ errorMessage  NOTIFY errorMessageChanged)
    Q_PROPERTY(QString keycardState  READ keycardState  NOTIFY keycardStateChanged)
    Q_PROPERTY(QString keycardStatus READ keycardStatusText NOTIFY keycardStateChanged)

public:
    explicit NotesBackend(QObject *parent = nullptr);

    QString currentScreen() const;
    QString errorMessage()  const;
    QString keycardState()  const;
    QString keycardStatusText() const;

    // Keycard reader detection
    Q_INVOKABLE QString startKeycardDetection();
    Q_INVOKABLE QString stopKeycardDetection();
    Q_INVOKABLE QString getKeycardState();

    // Keycard PIN authorization + key export
    Q_INVOKABLE QString keycardAuthorize(const QString &pin);
    Q_INVOKABLE QString keycardExportKey();

    // Keycard import: authorize card with PIN, export key, create account.
    // No separate app PIN — Keycard PIN is the only auth. Card must be present to unlock.
    Q_INVOKABLE void importFromKeycard(const QString &keycardPin,
                                        const QString &backupPath = {});

    // Keycard unlock: authorize card, re-derive key, decrypt notes.
    Q_INVOKABLE void unlockWithKeycard(const QString &keycardPin);

    // Keycard module integration: receive pre-derived key from keycard-basecamp
    Q_INVOKABLE void importWithKeycardKey(const QString &hexKey,
                                           const QString &backupPath = {});
    Q_INVOKABLE void unlockWithKeycardKey(const QString &hexKey);

    // Called from ImportScreen: validate mnemonic + PIN, derive key, save state.
    Q_INVOKABLE void importMnemonic(const QString &mnemonic,
                                    const QString &pin,
                                    const QString &pinConfirm,
                                    const QString &backupPath = {});

    // Called from UnlockScreen: re-derive key with PIN.
    Q_INVOKABLE void unlockWithPin(const QString &pin);

    // ── Note CRUD ──────────────────────────────────────────────────────
    Q_INVOKABLE QString createNote();
    Q_INVOKABLE QString loadNotes();
    Q_INVOKABLE QString loadNote(int id);
    Q_INVOKABLE QString saveNote(int id, const QString &plaintext);
    Q_INVOKABLE QString deleteNote(int id);

    // Lock session: wipe in-memory key, go back to unlock screen.
    Q_INVOKABLE void lock();

    // Short hex fingerprint derived from master key (for display in Settings).
    Q_INVOKABLE QString getAccountFingerprint() const;

    // Returns true if the database has a stored account (wrapped key or keycard).
    bool hasAccount() const;

    // Returns "keycard" or "mnemonic" (default).
    Q_INVOKABLE QString getKeySource() const;

    // Export all notes as an encrypted backup file. Returns the file path or error.
    Q_INVOKABLE QString exportBackup(const QString &filePath);

    // Export to well-known backups directory (no path argument needed).
    Q_INVOKABLE QString exportBackupAuto();

    // List .imnotes files in the backups directory.
    Q_INVOKABLE QString listBackups() const;

    // Import notes from an encrypted backup file.
    Q_INVOKABLE QString importBackup(const QString &filePath,
                                      const QString &mnemonic = {});

    // Well-known backups directory.
    static QString backupsDir();

    // Deterministic fingerprint from mnemonic (Ed25519 public key, first 8 bytes hex).
    // Same mnemonic → same fingerprint, always, on any device.
    static QString deriveFingerprint(const QString &mnemonic);

    // Wipe the database and return to the import screen.
    Q_INVOKABLE void resetAndWipe();

signals:
    void currentScreenChanged();
    void errorMessageChanged();
    void keycardStateChanged();

private:
    void setScreen(const QString &screen);
    void setError(const QString &msg);

    CryptoManager   m_crypto;
    DatabaseManager m_db;
    KeyManager      m_keys;
    KeycardBridge   m_keycard;

    QString m_currentScreen;
    QString m_errorMessage;

    void migratePlaintextTitles();

    // PIN brute-force protection (Issue #2)
    int  m_failedAttempts = 0;
    qint64 m_lockoutUntil = 0; // epoch seconds; 0 = no lockout
};
