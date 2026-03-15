#include <QtTest/QtTest>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/NotesBackend.h"
#include "core/KeyManager.h"

// Tests for account lifecycle (Issue #29).
// Drives NotesBackend directly: import, unlock, lock, reset, PIN lockout.

static const QString TEST_MNEMONIC =
    "abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon "
    "abandon about";

static const QString TEST_MNEMONIC_24 =
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon abandon art";

static const QString TEST_PIN = "123456";

class TestAccount : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ── Import ────────────────────────────────────────────────────────
    void testImportValidMnemonic();
    void testImportInvalidMnemonic();
    void testImportMismatchedPins();
    void testImportShortPin();
    void testImportSetsFingerprint();
    void testImportScreenTransition();

    // ── HasAccount / persistence ──────────────────────────────────────
    void testHasAccountAfterImport();
    void testHasAccountPersistsAcrossInstances();

    // ── Unlock ────────────────────────────────────────────────────────
    void testUnlockCorrectPin();
    void testUnlockWrongPin();
    void testUnlockShortPin();

    // ── Lock ──────────────────────────────────────────────────────────
    void testLockWipesKeyAndChangesScreen();
    void testNoteOperationsFailAfterLock();

    // ── PIN brute-force / lockout ─────────────────────────────────────
    void testWrongPinCountdown();
    void testLockoutAfterMaxAttempts();
    void testSuccessfulUnlockResetsCounter();
    void testLockoutPersistsAcrossInstances();

    // ── Reset ─────────────────────────────────────────────────────────
    void testResetAndWipeClearsAccount();
    void testResetReturnsToImportScreen();

    // ── Fingerprint ───────────────────────────────────────────────────
    void testFingerprintAvailableOnUnlockScreen();

private:
    void wipeTestData();
};

void TestAccount::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TestAccount::cleanupTestCase()
{
    wipeTestData();
    QStandardPaths::setTestModeEnabled(false);
}

void TestAccount::wipeTestData()
{
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir(dataDir).removeRecursively();
}

// ── Import ───────────────────────────────────────────────────────────────

void TestAccount::testImportValidMnemonic()
{
    wipeTestData();
    NotesBackend backend;
    QCOMPARE(backend.currentScreen(), "import");

    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "note");
    QVERIFY(backend.errorMessage().isEmpty());
}

void TestAccount::testImportInvalidMnemonic()
{
    wipeTestData();
    NotesBackend backend;

    backend.importMnemonic("invalid words that are not bip39", TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "import");
    QVERIFY(backend.errorMessage().contains("Invalid recovery phrase"));
}

void TestAccount::testImportMismatchedPins()
{
    wipeTestData();
    NotesBackend backend;

    backend.importMnemonic(TEST_MNEMONIC, "123456", "654321");
    QCOMPARE(backend.currentScreen(), "import");
    QVERIFY(backend.errorMessage().contains("PINs do not match"));
}

void TestAccount::testImportShortPin()
{
    wipeTestData();
    NotesBackend backend;

    backend.importMnemonic(TEST_MNEMONIC, "123", "123");
    QCOMPARE(backend.currentScreen(), "import");
    QVERIFY(backend.errorMessage().contains("at least"));
}

void TestAccount::testImportSetsFingerprint()
{
    wipeTestData();
    NotesBackend backend;
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);

    QString fp = backend.getAccountFingerprint();
    QCOMPARE(fp.length(), 64); // 32 bytes hex
    QVERIFY(!fp.isEmpty());

    // Same mnemonic always produces same fingerprint.
    QCOMPARE(fp, NotesBackend::deriveFingerprint(
        TEST_MNEMONIC.simplified()
            .normalized(QString::NormalizationForm_KD)
            .toLower()));
}

void TestAccount::testImportScreenTransition()
{
    wipeTestData();
    NotesBackend backend;
    QCOMPARE(backend.currentScreen(), "import");

    // Failed import stays on import screen.
    backend.importMnemonic("bad phrase", TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "import");

    // Successful import goes to note screen.
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "note");
}

// ── HasAccount / persistence ─────────────────────────────────────────────

void TestAccount::testHasAccountAfterImport()
{
    wipeTestData();
    NotesBackend backend;
    QVERIFY(!backend.hasAccount());

    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QVERIFY(backend.hasAccount());
}

void TestAccount::testHasAccountPersistsAcrossInstances()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
        QVERIFY(backend.hasAccount());
    }

    // New instance reads from persisted DB.
    NotesBackend backend2;
    QVERIFY(backend2.hasAccount());
    QCOMPARE(backend2.currentScreen(), "unlock");
}

// ── Unlock ───────────────────────────────────────────────────────────────

void TestAccount::testUnlockCorrectPin()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    NotesBackend backend;
    QCOMPARE(backend.currentScreen(), "unlock");

    backend.unlockWithPin(TEST_PIN);
    QCOMPARE(backend.currentScreen(), "note");
    QVERIFY(backend.errorMessage().isEmpty());
}

void TestAccount::testUnlockWrongPin()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    NotesBackend backend;
    backend.unlockWithPin("999999");
    QCOMPARE(backend.currentScreen(), "unlock");
    QVERIFY(backend.errorMessage().contains("Wrong PIN"));
}

void TestAccount::testUnlockShortPin()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    NotesBackend backend;
    backend.unlockWithPin("123");
    QCOMPARE(backend.currentScreen(), "unlock");
    QVERIFY(backend.errorMessage().contains("at least"));
}

// ── Lock ─────────────────────────────────────────────────────────────────

void TestAccount::testLockWipesKeyAndChangesScreen()
{
    wipeTestData();
    NotesBackend backend;
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "note");

    backend.lock();
    QCOMPARE(backend.currentScreen(), "unlock");
}

void TestAccount::testNoteOperationsFailAfterLock()
{
    wipeTestData();
    NotesBackend backend;
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);

    // Create a note while unlocked.
    QString result = backend.createNote();
    QVERIFY(!result.isEmpty());

    // Lock.
    backend.lock();

    // Note operations should fail.
    QVERIFY(backend.createNote().isEmpty());
    QCOMPARE(backend.loadNotes(), "[]");
}

// ── PIN brute-force / lockout ────────────────────────────────────────────

void TestAccount::testWrongPinCountdown()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    NotesBackend backend;

    // 5 attempts before lockout.
    for (int i = 1; i <= 4; ++i) {
        backend.unlockWithPin("999999");
        int remaining = 5 - i;
        QVERIFY(backend.errorMessage().contains(
            QString("%1 attempt(s) remaining").arg(remaining)));
    }
}

void TestAccount::testLockoutAfterMaxAttempts()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    NotesBackend backend;

    // Use up all 5 attempts.
    for (int i = 0; i < 5; ++i)
        backend.unlockWithPin("999999");

    // 5th wrong attempt triggers lockout.
    QVERIFY(backend.errorMessage().contains("Locked out"));
    QVERIFY(backend.errorMessage().contains("30 seconds"));
    QCOMPARE(backend.currentScreen(), "unlock");

    // Further attempts during lockout are rejected.
    backend.unlockWithPin(TEST_PIN);
    QVERIFY(backend.errorMessage().contains("Too many failed attempts"));
}

void TestAccount::testSuccessfulUnlockResetsCounter()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    // Fail 3 times, then succeed.
    {
        NotesBackend backend;
        for (int i = 0; i < 3; ++i)
            backend.unlockWithPin("999999");

        QVERIFY(backend.errorMessage().contains("2 attempt(s) remaining"));

        backend.unlockWithPin(TEST_PIN);
        QCOMPARE(backend.currentScreen(), "note");
        QVERIFY(backend.errorMessage().isEmpty());
    }

    // New instance: counter should be reset — 5 fresh attempts available.
    {
        NotesBackend backend;
        backend.unlockWithPin("999999");
        QVERIFY(backend.errorMessage().contains("4 attempt(s) remaining"));
    }
}

void TestAccount::testLockoutPersistsAcrossInstances()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    // Trigger lockout.
    {
        NotesBackend backend;
        for (int i = 0; i < 5; ++i)
            backend.unlockWithPin("999999");
        QVERIFY(backend.errorMessage().contains("Locked out"));
    }

    // New instance should still be locked out.
    {
        NotesBackend backend;
        backend.unlockWithPin(TEST_PIN); // Even correct PIN rejected during lockout.
        QVERIFY(backend.errorMessage().contains("Too many failed attempts"));
    }
}

// ── Reset ────────────────────────────────────────────────────────────────

void TestAccount::testResetAndWipeClearsAccount()
{
    wipeTestData();
    NotesBackend backend;
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QVERIFY(backend.hasAccount());

    backend.resetAndWipe();
    QVERIFY(!backend.hasAccount());
    QVERIFY(backend.getAccountFingerprint().isEmpty());
}

void TestAccount::testResetReturnsToImportScreen()
{
    wipeTestData();
    NotesBackend backend;
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "note");

    backend.resetAndWipe();
    QCOMPARE(backend.currentScreen(), "import");

    // Can re-import after reset.
    backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    QCOMPARE(backend.currentScreen(), "note");
}

// ── Fingerprint ──────────────────────────────────────────────────────────

void TestAccount::testFingerprintAvailableOnUnlockScreen()
{
    wipeTestData();
    {
        NotesBackend backend;
        backend.importMnemonic(TEST_MNEMONIC, TEST_PIN, TEST_PIN);
    }

    // On unlock screen (not unlocked), fingerprint should still be available
    // because it's stored in DB metadata, not derived from the in-memory key.
    NotesBackend backend;
    QCOMPARE(backend.currentScreen(), "unlock");

    QString fp = backend.getAccountFingerprint();
    QCOMPARE(fp.length(), 64);
    QVERIFY(!fp.isEmpty());
}

QTEST_MAIN(TestAccount)
#include "test_account.moc"
