#pragma once

#include <QByteArray>
#include <QString>

// Manages the SQLite database that stores the encrypted note.
// Schema:
//   notes(id INTEGER PRIMARY KEY, ciphertext BLOB, nonce BLOB)
//   wrapped_key(id INTEGER PRIMARY KEY,
//               ciphertext BLOB,   -- master key encrypted with PIN-derived key
//               nonce      BLOB,   -- AES-GCM nonce for the wrapping operation
//               pin_salt   BLOB)   -- Argon2id salt used to derive the PIN key
//   meta(key TEXT PRIMARY KEY, value TEXT)   -- stores "initialized" flag
class DatabaseManager
{
public:
    explicit DatabaseManager(const QString &dbPath = QString());

    // Open (or create) the database and run migrations.
    bool init();

    // Returns true if the account has been set up (mnemonic imported).
    bool isInitialized() const;

    // Persist the encrypted note. Upserts row with id=1.
    bool saveNote(const QByteArray &ciphertext, const QByteArray &nonce);

    // Load the encrypted note. Returns false if no note exists yet.
    bool loadNote(QByteArray &ciphertextOut, QByteArray &nonceOut) const;

    // Mark the account as initialized (called after successful import).
    bool setInitialized();

    // Persist the PIN-wrapped master key produced during import.
    bool saveWrappedKey(const QByteArray &ciphertext,
                        const QByteArray &nonce,
                        const QByteArray &pinSalt);

    // Load the PIN-wrapped master key for use during unlock.
    // Returns false if no wrapped key has been stored yet.
    bool loadWrappedKey(QByteArray &ciphertextOut,
                        QByteArray &nonceOut,
                        QByteArray &pinSaltOut) const;

    // Close the connection and delete the database file.
    void wipe();

    QString dbPath() const { return m_dbPath; }

private:
    QString m_dbPath;
    bool    m_ready = false;
};
