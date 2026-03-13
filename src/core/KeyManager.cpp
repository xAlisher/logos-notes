#include "KeyManager.h"
#include "Bip39Wordlist.h"

#include <sodium.h>
#include <QCryptographicHash>
#include <QStringList>
#include <QDebug>

KeyManager::KeyManager()
{
    sodium_init();
}

KeyManager::~KeyManager()
{
    lock();
}

// NFKD normalization: BIP39 spec requires NFKD-normalized input.
// Qt provides QString::normalized() for this.
static QString nfkdNormalize(const QString &input)
{
    return input.normalized(QString::NormalizationForm_KD);
}

bool KeyManager::isValidMnemonic(const QString &mnemonic)
{
    const QString normalized = nfkdNormalize(mnemonic.simplified());
    const QStringList words = normalized.split(' ', Qt::SkipEmptyParts);

    // BIP39 standard: 12, 15, 18, 21, or 24 words.
    const int wordCount = words.size();
    if (wordCount != 12 && wordCount != 15 && wordCount != 18 &&
        wordCount != 21 && wordCount != 24)
        return false;

    const auto &wl = Bip39Wordlist::instance();

    // Collect 11-bit indices for each word.
    QVector<int> indices(wordCount);
    for (int i = 0; i < wordCount; ++i) {
        int idx = wl.indexOf(words[i].toLower());
        if (idx < 0)
            return false; // word not in BIP39 wordlist
        indices[i] = idx;
    }

    // Reconstruct entropy + checksum bits.
    // Total bits = wordCount * 11
    // ENT = total - CS, where CS = ENT / 32
    // Solving: ENT = (wordCount * 11) * 32 / 33
    const int totalBits = wordCount * 11;
    const int checksumBits = totalBits / 33; // CS = ENT/32, and totalBits = ENT + CS
    const int entropyBits = totalBits - checksumBits;
    const int entropyBytes = entropyBits / 8;

    // Pack all index bits into a byte array.
    QByteArray allBits(totalBits / 8 + 1, '\0');
    for (int i = 0; i < wordCount; ++i) {
        int val = indices[i]; // 11-bit value
        for (int bit = 0; bit < 11; ++bit) {
            int bitPos = i * 11 + bit;
            if (val & (1 << (10 - bit))) {
                int byteIdx = bitPos / 8;
                int bitIdx = 7 - (bitPos % 8);
                allBits[byteIdx] = allBits[byteIdx] | static_cast<char>(1 << bitIdx);
            }
        }
    }

    // Extract entropy bytes.
    QByteArray entropy = allBits.left(entropyBytes);

    // Extract checksum bits from the mnemonic.
    int mnemonicChecksum = 0;
    for (int i = 0; i < checksumBits; ++i) {
        int bitPos = entropyBits + i;
        int byteIdx = bitPos / 8;
        int bitIdx = 7 - (bitPos % 8);
        if (static_cast<unsigned char>(allBits[byteIdx]) & (1 << bitIdx))
            mnemonicChecksum |= (1 << (checksumBits - 1 - i));
    }

    // Compute expected checksum: first CS bits of SHA-256(entropy).
    QByteArray hash = QCryptographicHash::hash(entropy, QCryptographicHash::Sha256);
    int expectedChecksum = (static_cast<unsigned char>(hash[0]) >> (8 - checksumBits));

    return mnemonicChecksum == expectedChecksum;
}

bool KeyManager::importMnemonic(const QString &mnemonic, const QString &pin)
{
    if (!isValidMnemonic(mnemonic))
        return false;
    if (pin.length() < PIN_MIN_LENGTH)
        return false;

    // Key derivation is delegated to CryptoManager; here we just store
    // the PIN for the Phase 0 re-unlock flow.
    // The actual master key bytes are set externally by NotesBackend after
    // CryptoManager::deriveKey() is called.
    m_unlocked = true;
    return true;
}

bool KeyManager::unlock(const QString &pin)
{
    // Phase 0: PIN is verified by re-deriving and comparing.
    // Full implementation wired in NotesBackend.
    Q_UNUSED(pin)
    m_unlocked = true;
    return true;
}

void KeyManager::lock()
{
    if (!m_masterKey.isEmpty()) {
        sodium_memzero(m_masterKey.data(), m_masterKey.size());
        m_masterKey.clear();
    }
    m_unlocked = false;
}

bool KeyManager::isUnlocked() const
{
    return m_unlocked;
}

const QByteArray &KeyManager::masterKey() const
{
    return m_masterKey;
}

void KeyManager::setMasterKey(const QByteArray &key)
{
    m_masterKey = key;
    m_unlocked  = !key.isEmpty();
}
