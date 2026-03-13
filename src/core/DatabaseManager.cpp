#include "DatabaseManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDebug>

static constexpr char CONNECTION[] = "logos-notes";

DatabaseManager::DatabaseManager(const QString &dbPath)
{
    if (dbPath.isEmpty()) {
        const QString dataDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        m_dbPath = dataDir + "/notes.db";
    } else {
        m_dbPath = dbPath;
    }
}

bool DatabaseManager::init()
{
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", CONNECTION);
    db.setDatabaseName(m_dbPath);

    if (!db.open()) {
        qWarning() << "DatabaseManager: cannot open" << m_dbPath << db.lastError();
        return false;
    }

    QSqlQuery q(db);

    // Phase 0 schema
    if (!q.exec("CREATE TABLE IF NOT EXISTS notes ("
                "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  ciphertext BLOB NOT NULL,"
                "  nonce      BLOB NOT NULL"
                ")")) {
        qWarning() << "DatabaseManager: create notes table failed:" << q.lastError();
        return false;
    }
    if (!q.exec("CREATE TABLE IF NOT EXISTS wrapped_key ("
                "  id         INTEGER PRIMARY KEY,"
                "  ciphertext BLOB NOT NULL,"
                "  nonce      BLOB NOT NULL,"
                "  pin_salt   BLOB NOT NULL"
                ")")) {
        qWarning() << "DatabaseManager: create wrapped_key table failed:" << q.lastError();
        return false;
    }
    if (!q.exec("CREATE TABLE IF NOT EXISTS meta ("
                "  key   TEXT PRIMARY KEY,"
                "  value TEXT NOT NULL"
                ")")) {
        qWarning() << "DatabaseManager: create meta table failed:" << q.lastError();
        return false;
    }

    // Phase 1 migration: add title + updated_at columns if missing.
    // PRAGMA table_info returns column names; check if updated_at exists.
    bool hasUpdatedAt = false;
    if (q.exec("PRAGMA table_info(notes)")) {
        while (q.next()) {
            if (q.value(1).toString() == "updated_at") {
                hasUpdatedAt = true;
                break;
            }
        }
    }
    if (!hasUpdatedAt) {
        if (!q.exec("ALTER TABLE notes ADD COLUMN title TEXT NOT NULL DEFAULT ''")) {
            qWarning() << "DatabaseManager: migration add title failed:" << q.lastError();
            return false;
        }
        if (!q.exec("ALTER TABLE notes ADD COLUMN updated_at INTEGER NOT NULL DEFAULT 0")) {
            qWarning() << "DatabaseManager: migration add updated_at failed:" << q.lastError();
            return false;
        }
        qDebug() << "DatabaseManager: migrated notes table (added title, updated_at)";
    }

    m_ready = true;
    return true;
}

bool DatabaseManager::isInitialized() const
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("SELECT value FROM meta WHERE key = 'initialized'");
    if (!q.exec() || !q.next())
        return false;
    return q.value(0).toString() == "1";
}

bool DatabaseManager::saveNote(const QByteArray &ciphertext, const QByteArray &nonce)
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO notes (id, ciphertext, nonce) VALUES (1, ?, ?)");
    q.addBindValue(ciphertext);
    q.addBindValue(nonce);
    if (!q.exec()) {
        qWarning() << "DatabaseManager: saveNote failed:" << q.lastError();
        return false;
    }
    return true;
}

bool DatabaseManager::loadNote(QByteArray &ciphertextOut, QByteArray &nonceOut) const
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("SELECT ciphertext, nonce FROM notes WHERE id = 1");
    if (!q.exec() || !q.next())
        return false;
    ciphertextOut = q.value(0).toByteArray();
    nonceOut      = q.value(1).toByteArray();
    return true;
}

bool DatabaseManager::saveWrappedKey(const QByteArray &ciphertext,
                                      const QByteArray &nonce,
                                      const QByteArray &pinSalt)
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO wrapped_key (id, ciphertext, nonce, pin_salt)"
              " VALUES (1, ?, ?, ?)");
    q.addBindValue(ciphertext);
    q.addBindValue(nonce);
    q.addBindValue(pinSalt);
    if (!q.exec()) {
        qWarning() << "DatabaseManager: saveWrappedKey failed:" << q.lastError();
        return false;
    }
    return true;
}

bool DatabaseManager::loadWrappedKey(QByteArray &ciphertextOut,
                                      QByteArray &nonceOut,
                                      QByteArray &pinSaltOut) const
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("SELECT ciphertext, nonce, pin_salt FROM wrapped_key WHERE id = 1");
    if (!q.exec() || !q.next())
        return false;
    ciphertextOut = q.value(0).toByteArray();
    nonceOut      = q.value(1).toByteArray();
    pinSaltOut    = q.value(2).toByteArray();
    return true;
}

void DatabaseManager::wipe()
{
    {
        QSqlDatabase db = QSqlDatabase::database(CONNECTION);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(CONNECTION);
    QFile::remove(m_dbPath);
    m_ready = false;
}

bool DatabaseManager::setInitialized()
{
    return saveMeta("initialized", "1");
}

bool DatabaseManager::saveMeta(const QString &key, const QString &value)
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)");
    q.addBindValue(key);
    q.addBindValue(value);
    if (!q.exec()) {
        qWarning() << "DatabaseManager: saveMeta failed:" << q.lastError();
        return false;
    }
    return true;
}

QString DatabaseManager::loadMeta(const QString &key, const QString &defaultValue) const
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("SELECT value FROM meta WHERE key = ?");
    q.addBindValue(key);
    if (!q.exec() || !q.next())
        return defaultValue;
    return q.value(0).toString();
}

bool DatabaseManager::saveMetaBlob(const QString &key, const QByteArray &blob)
{
    return saveMeta(key, QString::fromLatin1(blob.toBase64()));
}

QByteArray DatabaseManager::loadMetaBlob(const QString &key) const
{
    QString b64 = loadMeta(key);
    if (b64.isEmpty())
        return {};
    return QByteArray::fromBase64(b64.toLatin1());
}

// ── Phase 1: multi-note CRUD ────────────────────────────────────────────

int DatabaseManager::createNote()
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    // Insert an empty note with zero-length ciphertext/nonce.
    qint64 now = QDateTime::currentSecsSinceEpoch();
    q.prepare("INSERT INTO notes (ciphertext, nonce, title, updated_at)"
              " VALUES (X'', X'', '', ?)");
    q.addBindValue(now);
    if (!q.exec()) {
        qWarning() << "DatabaseManager: createNote failed:" << q.lastError();
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool DatabaseManager::saveNote(int id, const QByteArray &ciphertext,
                                const QByteArray &nonce, const QString &title)
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    qint64 now = QDateTime::currentSecsSinceEpoch();
    q.prepare("UPDATE notes SET ciphertext = ?, nonce = ?, title = ?, updated_at = ?"
              " WHERE id = ?");
    q.addBindValue(ciphertext);
    q.addBindValue(nonce);
    q.addBindValue(title);
    q.addBindValue(now);
    q.addBindValue(id);
    if (!q.exec()) {
        qWarning() << "DatabaseManager: saveNote(id) failed:" << q.lastError();
        return false;
    }
    return q.numRowsAffected() > 0;
}

bool DatabaseManager::loadNote(int id, QByteArray &ciphertextOut,
                                QByteArray &nonceOut) const
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("SELECT ciphertext, nonce FROM notes WHERE id = ?");
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return false;
    ciphertextOut = q.value(0).toByteArray();
    nonceOut      = q.value(1).toByteArray();
    return true;
}

bool DatabaseManager::deleteNote(int id)
{
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("DELETE FROM notes WHERE id = ?");
    q.addBindValue(id);
    if (!q.exec()) {
        qWarning() << "DatabaseManager: deleteNote failed:" << q.lastError();
        return false;
    }
    return q.numRowsAffected() > 0;
}

QList<NoteHeader> DatabaseManager::loadNoteHeaders() const
{
    QList<NoteHeader> result;
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("SELECT id, title, updated_at FROM notes ORDER BY updated_at DESC");
    if (!q.exec())
        return result;
    while (q.next()) {
        result.append({q.value(0).toInt(),
                       q.value(1).toString(),
                       q.value(2).toLongLong()});
    }
    return result;
}
