#include "DatabaseManager.h"

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
    if (!q.exec("CREATE TABLE IF NOT EXISTS notes ("
                "  id        INTEGER PRIMARY KEY,"
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
    QSqlDatabase db = QSqlDatabase::database(CONNECTION);
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO meta (key, value) VALUES ('initialized', '1')");
    if (!q.exec()) {
        qWarning() << "DatabaseManager: setInitialized failed:" << q.lastError();
        return false;
    }
    return true;
}
