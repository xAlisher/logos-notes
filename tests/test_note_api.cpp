#include <QtTest/QtTest>
#include <QStandardPaths>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "core/NotesBackend.h"

// Tests for note CRUD API behavior (Issue #28).
// Drives NotesBackend directly using QStandardPaths test mode.

static const QString TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon "
    "abandon about";

static const QString TEST_PIN = "123456";

static QJsonObject parseJson(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

static QJsonArray parseJsonArray(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).array();
}

class TestNoteApi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── CRUD behavior ────────────────────────────────────────────────
    void testCreateNoteReturnsId();
    void testSaveAndLoadNote();
    void testLoadNotesReturnsHeaders();
    void testTitleDerivedFromFirstLine();
    void testEmptyNoteLoadable();
    void testDeleteNote();
    void testDeleteNonexistentNote();

    // ── Locked-state failures ────────────────────────────────────────
    void testCreateNoteRequiresUnlock();
    void testLoadNotesRequiresUnlock();
    void testSaveNoteRequiresUnlock();
    void testLoadNoteRequiresUnlock();
    void testDeleteNoteRequiresUnlock();

    // ── Ordering ─────────────────────────────────────────────────────
    void testMultipleNotesOrdering();

    // ── Legacy migration ─────────────────────────────────────────────
    void testLegacyPlaintextTitleMigration();

private:
    void wipeTestData();
    void importAndUnlock(NotesBackend &backend);
};

void TestNoteApi::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestNoteApi::cleanupTestCase()
{
    wipeTestData();
    QStandardPaths::setTestModeEnabled(false);
}

void TestNoteApi::wipeTestData()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(dataDir).removeRecursively();
}

void TestNoteApi::importAndUnlock(NotesBackend &backend)
{
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "note");
}

// ── CRUD behavior ────────────────────────────────────────────────────────

void TestNoteApi::testCreateNoteReturnsId()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QString result = backend.createNote();
    QVERIFY(!result.isEmpty());

    QJsonObject obj = parseJson(result);
    QVERIFY(obj.contains("id"));
    QVERIFY(obj["id"].toInt() > 0);
}

void TestNoteApi::testSaveAndLoadNote()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create a note.
    QJsonObject created = parseJson(backend.createNote());
    int noteId = created["id"].toInt();
    QVERIFY(noteId > 0);

    // Save content.
    QString saveResult = backend.saveNote(noteId, "Hello, encrypted world!");
    QCOMPARE(saveResult, "ok");

    // Load it back.
    QString loaded = backend.loadNote(noteId);
    QCOMPARE(loaded, "Hello, encrypted world!");
}

void TestNoteApi::testLoadNotesReturnsHeaders()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create two notes with content.
    QJsonObject n1 = parseJson(backend.createNote());
    QJsonObject n2 = parseJson(backend.createNote());
    backend.saveNote(n1["id"].toInt(), "First note");
    backend.saveNote(n2["id"].toInt(), "Second note");

    // Load the list.
    QJsonArray notes = parseJsonArray(backend.loadNotes());
    QCOMPARE(notes.size(), 2);

    // Each entry must have id, title, updatedAt.
    for (const auto &v : notes) {
        QJsonObject entry = v.toObject();
        QVERIFY(entry.contains("id"));
        QVERIFY(entry.contains("title"));
        QVERIFY(entry.contains("updatedAt"));
        QVERIFY(entry["id"].toInt() > 0);
        QVERIFY(entry["updatedAt"].toInt() > 0);
    }
}

void TestNoteApi::testTitleDerivedFromFirstLine()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    QJsonObject created = parseJson(backend.createNote());
    int noteId = created["id"].toInt();

    // Save content where the first non-empty line is "My Title".
    backend.saveNote(noteId, "\n\nMy Title\nBody text here");

    // The title in loadNotes should be "My Title".
    QJsonArray notes = parseJsonArray(backend.loadNotes());
    QCOMPARE(notes.size(), 1);

    QJsonObject entry = notes[0].toObject();
    QCOMPARE(entry["title"].toString(), "My Title");
}

void TestNoteApi::testEmptyNoteLoadable()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create a note but never save any content.
    QJsonObject created = parseJson(backend.createNote());
    int noteId = created["id"].toInt();

    // Loading it should return empty string (no content yet).
    QString loaded = backend.loadNote(noteId);
    QVERIFY(loaded.isEmpty());
}

void TestNoteApi::testDeleteNote()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create and save a note.
    QJsonObject created = parseJson(backend.createNote());
    int noteId = created["id"].toInt();
    backend.saveNote(noteId, "Delete me");

    // Verify it exists.
    QJsonArray beforeDelete = parseJsonArray(backend.loadNotes());
    QCOMPARE(beforeDelete.size(), 1);

    // Delete it.
    QString deleteResult = backend.deleteNote(noteId);
    QCOMPARE(deleteResult, "ok");

    // Verify it is gone from the list.
    QJsonArray afterDelete = parseJsonArray(backend.loadNotes());
    QCOMPARE(afterDelete.size(), 0);

    // Loading it should return empty (note no longer exists).
    QString loaded = backend.loadNote(noteId);
    QVERIFY(loaded.isEmpty());
}

void TestNoteApi::testDeleteNonexistentNote()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Delete a note ID that was never created.
    QString result = backend.deleteNote(99999);
    // Should return empty (not "ok") since no row was affected.
    // The backend returns {} (null/empty QString) because
    // DatabaseManager::deleteNote checks numRowsAffected > 0.
    QVERIFY(result.isEmpty());
}

// ── Locked-state failures ────────────────────────────────────────────────

void TestNoteApi::testCreateNoteRequiresUnlock()
{
    wipeTestData();
    NotesBackend backend;
    // Don't import — backend is locked.

    QString result = backend.createNote();
    QVERIFY(result.isEmpty());
}

void TestNoteApi::testLoadNotesRequiresUnlock()
{
    wipeTestData();
    NotesBackend backend;

    QString result = backend.loadNotes();
    QCOMPARE(result, "[]");
}

void TestNoteApi::testSaveNoteRequiresUnlock()
{
    wipeTestData();
    NotesBackend backend;

    QString result = backend.saveNote(1, "Should not save");
    QVERIFY(result.isEmpty());
}

void TestNoteApi::testLoadNoteRequiresUnlock()
{
    wipeTestData();
    NotesBackend backend;

    QString result = backend.loadNote(1);
    QVERIFY(result.isEmpty());
}

void TestNoteApi::testDeleteNoteRequiresUnlock()
{
    wipeTestData();
    NotesBackend backend;

    QString result = backend.deleteNote(1);
    QVERIFY(result.isEmpty());
}

// ── Ordering ─────────────────────────────────────────────────────────────

void TestNoteApi::testMultipleNotesOrdering()
{
    wipeTestData();
    NotesBackend backend;
    importAndUnlock(backend);

    // Create 3 notes and save them with delays so updated_at (epoch seconds) differs.
    QJsonObject n1 = parseJson(backend.createNote());
    backend.saveNote(n1["id"].toInt(), "Note A");

    // updated_at is epoch seconds — need >1s delay for distinct timestamps.
    QTest::qWait(1100);

    QJsonObject n2 = parseJson(backend.createNote());
    backend.saveNote(n2["id"].toInt(), "Note B");

    QTest::qWait(1100);

    QJsonObject n3 = parseJson(backend.createNote());
    backend.saveNote(n3["id"].toInt(), "Note C");

    // loadNotes should return most recent first.
    QJsonArray notes = parseJsonArray(backend.loadNotes());
    QCOMPARE(notes.size(), 3);

    // Verify descending order by updatedAt.
    int prevTimestamp = INT_MAX;
    for (const auto &v : notes) {
        int ts = v.toObject()["updatedAt"].toInt();
        QVERIFY2(ts <= prevTimestamp,
                 qPrintable(QString("Notes not in descending order: %1 > %2")
                     .arg(ts).arg(prevTimestamp)));
        prevTimestamp = ts;
    }

    // Most recent note should be "Note C".
    QCOMPARE(notes[0].toObject()["title"].toString(), "Note C");
}

// ── Legacy migration ─────────────────────────────────────────────────────

void TestNoteApi::testLegacyPlaintextTitleMigration()
{
    // Seed a legacy note with plaintext title column populated but
    // encrypted title columns empty. On unlock, migratePlaintextTitles()
    // should encrypt the title and keep it readable via loadNotes().
    wipeTestData();

    // Step 1: Create an account and a note with content (to have a valid row).
    {
        NotesBackend backend;
        importAndUnlock(backend);

        QJsonObject n = parseJson(backend.createNote());
        backend.saveNote(n["id"].toInt(), "Legacy note body");
    }

    // Step 2: Directly manipulate the DB to simulate a legacy row:
    // set the plaintext title column and clear encrypted title columns.
    {
        QString dbPath = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) + "/notes.db";
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "legacy-test");
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());

        QSqlQuery q(db);
        QVERIFY(q.exec("UPDATE notes SET title = 'Legacy Title', "
                        "title_ciphertext = '', title_nonce = ''"));
        db.close();
        QSqlDatabase::removeDatabase("legacy-test");
    }

    // Step 3: Create a new backend — it reads persisted state, shows unlock screen.
    // Unlock triggers migratePlaintextTitles().
    {
        NotesBackend backend;
        QCOMPARE(backend.currentScreen(), "unlock");

        backend.unlockWithPin(TEST_PIN);
        QCOMPARE(backend.currentScreen(), "note");

        // loadNotes should still return the title — now encrypted.
        QJsonArray notes = parseJsonArray(backend.loadNotes());
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes[0].toObject()["title"].toString(), "Legacy Title");
    }

    // Step 4: Verify the DB no longer has plaintext title.
    {
        QString dbPath = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) + "/notes.db";
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "legacy-verify");
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());

        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT title, title_ciphertext FROM notes LIMIT 1"));
        QVERIFY(q.next());

        // Encrypted title columns should now be populated.
        QVERIFY(!q.value(1).toByteArray().isEmpty());

        db.close();
        QSqlDatabase::removeDatabase("legacy-verify");
    }
}

QTEST_MAIN(TestNoteApi)
#include "test_note_api.moc"
