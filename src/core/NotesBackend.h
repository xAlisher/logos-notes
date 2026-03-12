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
                                    const QString &pinConfirm);

    // Called from UnlockScreen: re-derive key with PIN.
    Q_INVOKABLE void unlockWithPin(const QString &pin);

    // Called from NoteScreen: encrypt and persist the note.
    Q_INVOKABLE void saveNote(const QString &plaintext);

    // Called from NoteScreen on load: decrypt and return the note.
    Q_INVOKABLE QString loadNote();

    // Lock session: wipe in-memory key, go back to unlock screen.
    Q_INVOKABLE void lock();

    // Returns true if the database has a stored account (wrapped key).
    bool hasAccount() const;

    // DEV/DEMO: wipe the database and return to the import screen.
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
};
