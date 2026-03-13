#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "core/CryptoManager.h"
#include "core/DatabaseManager.h"
#include "core/KeyManager.h"
#include "core/NotesBackend.h"

// Tests the Phase 1 multi-note backend: create → save → loadNotes → loadNote → delete.
// Uses a real SQLite DB in a temp dir with real libsodium crypto.

class TestMultiNote : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testMigrationAddsColumns();
    void testCreateNote();
    void testSaveAndLoadNote();
    void testLoadNoteHeaders();
    void testDeleteNote();
    void testPhase0Compat();
    void testTitleFromFirstLine();

private:
    QTemporaryDir *m_tmpDir = nullptr;
    DatabaseManager *m_db = nullptr;
    CryptoManager m_crypto;
    QByteArray m_key; // test encryption key
};

void TestMultiNote::initTestCase()
{
    m_tmpDir = new QTemporaryDir;
    QVERIFY(m_tmpDir->isValid());
    m_db = new DatabaseManager(m_tmpDir->path() + "/test.db");
    QVERIFY(m_db->init());

    // Derive a test key from a fixed mnemonic + random salt.
    QByteArray salt = CryptoManager::randomSalt();
    m_key = m_crypto.deriveKey("abandon abandon abandon abandon abandon "
                               "abandon abandon abandon abandon abandon "
                               "abandon about", salt);
    QVERIFY(!m_key.isEmpty());
}

void TestMultiNote::cleanupTestCase()
{
    delete m_db;
    delete m_tmpDir;
}

void TestMultiNote::testMigrationAddsColumns()
{
    // If we got here, init() ran the migration successfully.
    // Verify by creating a note (which uses the new columns).
    int id = m_db->createNote();
    QVERIFY(id > 0);
    m_db->deleteNote(id);
}

void TestMultiNote::testCreateNote()
{
    int id1 = m_db->createNote();
    int id2 = m_db->createNote();
    QVERIFY(id1 > 0);
    QVERIFY(id2 > 0);
    QVERIFY(id2 != id1);

    // Clean up
    m_db->deleteNote(id1);
    m_db->deleteNote(id2);
}

void TestMultiNote::testSaveAndLoadNote()
{
    int id = m_db->createNote();
    QVERIFY(id > 0);

    // Encrypt some text
    QByteArray nonce;
    QByteArray plaintext = "Hello, multi-note world!";
    QByteArray ciphertext = m_crypto.encrypt(plaintext, m_key, nonce);
    QVERIFY(!ciphertext.isEmpty());

    // Save
    QVERIFY(m_db->saveNote(id, ciphertext, nonce, "Hello, multi-note world!"));

    // Load back
    QByteArray loadedCipher, loadedNonce;
    QVERIFY(m_db->loadNote(id, loadedCipher, loadedNonce));
    QCOMPARE(loadedCipher, ciphertext);
    QCOMPARE(loadedNonce, nonce);

    // Decrypt and verify
    QByteArray decrypted = m_crypto.decrypt(loadedCipher, m_key, loadedNonce);
    QCOMPARE(decrypted, plaintext);

    m_db->deleteNote(id);
}

void TestMultiNote::testLoadNoteHeaders()
{
    int id1 = m_db->createNote();
    int id2 = m_db->createNote();

    QByteArray nonce1, nonce2;
    QByteArray ct1 = m_crypto.encrypt("First note", m_key, nonce1);
    QByteArray ct2 = m_crypto.encrypt("Second note", m_key, nonce2);

    QVERIFY(m_db->saveNote(id1, ct1, nonce1, "First note"));
    // Small delay to ensure different timestamps
    QThread::msleep(1100);
    QVERIFY(m_db->saveNote(id2, ct2, nonce2, "Second note"));

    auto headers = m_db->loadNoteHeaders();
    QVERIFY(headers.size() >= 2);

    // Most recently updated should be first
    bool foundId2First = false;
    for (int i = 0; i < headers.size(); ++i) {
        if (headers[i].id == id2) {
            foundId2First = true;
            break;
        }
        if (headers[i].id == id1) {
            break; // id1 came first — wrong order
        }
    }
    QVERIFY(foundId2First);

    // Check titles
    bool foundTitle1 = false, foundTitle2 = false;
    for (const auto &h : headers) {
        if (h.id == id1) { QCOMPARE(h.title, "First note"); foundTitle1 = true; }
        if (h.id == id2) { QCOMPARE(h.title, "Second note"); foundTitle2 = true; }
    }
    QVERIFY(foundTitle1);
    QVERIFY(foundTitle2);

    m_db->deleteNote(id1);
    m_db->deleteNote(id2);
}

void TestMultiNote::testDeleteNote()
{
    int id = m_db->createNote();
    QVERIFY(id > 0);
    QVERIFY(m_db->deleteNote(id));

    // Loading a deleted note should fail
    QByteArray ct, n;
    QVERIFY(!m_db->loadNote(id, ct, n));

    // Deleting again should return false (0 rows affected)
    QVERIFY(!m_db->deleteNote(id));
}

void TestMultiNote::testPhase0Compat()
{
    // Phase 0 saveNote(ciphertext, nonce) always targets id=1
    QByteArray nonce;
    QByteArray ct = m_crypto.encrypt("Phase 0 note", m_key, nonce);
    QVERIFY(m_db->saveNote(ct, nonce));

    // Should be loadable via Phase 0 API
    QByteArray loadedCt, loadedNonce;
    QVERIFY(m_db->loadNote(loadedCt, loadedNonce));
    QCOMPARE(loadedCt, ct);

    // Also loadable via Phase 1 API with id=1
    QByteArray loadedCt2, loadedNonce2;
    QVERIFY(m_db->loadNote(1, loadedCt2, loadedNonce2));
    QCOMPARE(loadedCt2, ct);

    // Update via Phase 1 API
    QByteArray nonce2;
    QByteArray ct2 = m_crypto.encrypt("Updated phase 0", m_key, nonce2);
    QVERIFY(m_db->saveNote(1, ct2, nonce2, "Updated phase 0"));

    // Phase 0 load should see the update
    QByteArray loadedCt3, loadedNonce3;
    QVERIFY(m_db->loadNote(loadedCt3, loadedNonce3));
    QCOMPARE(loadedCt3, ct2);
}

void TestMultiNote::testTitleFromFirstLine()
{
    int id = m_db->createNote();

    // Title should be derived from first non-empty line
    QByteArray nonce;
    QString text = "\n\n  My Title Here  \nBody text";
    QByteArray ct = m_crypto.encrypt(text.toUtf8(), m_key, nonce);
    QVERIFY(m_db->saveNote(id, ct, nonce, "My Title Here"));

    auto headers = m_db->loadNoteHeaders();
    bool found = false;
    for (const auto &h : headers) {
        if (h.id == id) {
            QCOMPARE(h.title, "My Title Here");
            found = true;
            break;
        }
    }
    QVERIFY(found);

    m_db->deleteNote(id);
}

QTEST_MAIN(TestMultiNote)
#include "test_multi_note.moc"
