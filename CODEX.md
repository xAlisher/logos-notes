# Codex Reviewer Guide — Immutable Notes

> Read this file before reviewing any code in this repo.

## Your Role

You are the security reviewer and code auditor for this project. Claude Code (Opus) is the implementer. The human (Alisher) is the architect and decision-maker. You review diffs, run tests, and post findings as GitHub issue comments.

## Project Summary

Encrypted, local-first notes app for the Logos ecosystem. Qt6/QML desktop app that runs both standalone and as a module inside Logos App (a Qt-based module host).

**Repo:** https://github.com/xAlisher/logos-notes
**Stack:** C++17, Qt 6.9.3, QML, libsodium 1.0.18, SQLite, CMake + Ninja

---

## How to Build and Test

```bash
# Configure
cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=~/Qt/6.9.3/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run tests (two test binaries)
./build/test_multi_note    # DB/CRUD tests
./build/test_security      # Crypto, BIP39, fingerprint, PIN tests

# Lint plugin QML (catches syntax errors that crash Logos App silently)
~/Qt/6.9.3/gcc_64/bin/qmllint plugins/notes_ui/Main.qml
```

Test count should be **25** (8 multi-note + 17 security) as of v0.5.1.

---

## Architecture

```
src/core/
├── CryptoManager.h/cpp    — AES-256-GCM + Argon2id (libsodium)
├── DatabaseManager.h/cpp  — SQLite: notes, wrapped_key, meta tables
├── KeyManager.h/cpp       — BIP39 validation + checksum, key lifecycle
├── NotesBackend.h/cpp     — Main backend: screens, notes, backup, PIN lockout
├── SecureBuffer.h         — RAII wrapper, sodium_memzero on destruction
└── Bip39Wordlist.h        — Embedded 2048-word BIP39 English wordlist

src/plugin/
├── NotesPlugin.h/cpp      — PluginInterface for Logos App (Q_INVOKABLE surface)
└── plugin_metadata.json

plugins/notes_ui/
└── Main.qml               — All UI screens for Logos App module (ui_qml type)

src/ui/screens/             — Standalone app QML (uses Logos.Theme/Controls)
```

### Key design rules

1. **NotesPlugin is the only surface QML can see.** Every backend method QML needs must be `Q_INVOKABLE` on `NotesPlugin`. Missing methods fail silently — no error, just null response.

2. **Nothing sensitive in plaintext on disk.** Note content, titles, and mnemonic are all encrypted. Only ciphertext + nonces + wrapped key + KDF salts in DB.

3. **Normalize mnemonic before all crypto.** `normalizeMnemonic()` (NFKD + simplified whitespace + lowercase) must be called before key derivation, fingerprint derivation, and backup re-derivation. Without this, same phrase with different spacing → different keys → data loss.

4. **AES-256-GCM only, fail-fast.** No cipher fallback. `crypto_aead_aes256gcm_is_available()` checked at startup. If unavailable, app refuses to start.

---

## Encryption Flow

```
BIP39 mnemonic
    └─ normalize (NFKD, lowercase, whitespace)
    └─ Argon2id (random persisted salt, OPSLIMIT_MODERATE)
           └─ 256-bit master key (never stored)

PIN (min 6 characters)
    └─ Argon2id (random salt, OPSLIMIT_MODERATE)
           └─ 256-bit wrapping key
                  └─ AES-256-GCM(master key) → stored in DB

Note content + title
    └─ AES-256-GCM(plaintext, master key, random nonce) → stored in DB

Identity
    └─ SHA-256(normalized mnemonic) → Ed25519 seed → public key
```

---

## Database Schema

```sql
notes(id INTEGER PRIMARY KEY AUTOINCREMENT,
      ciphertext BLOB, nonce BLOB,
      title TEXT DEFAULT '',              -- legacy, cleared on save
      updated_at INTEGER DEFAULT 0,
      title_ciphertext BLOB DEFAULT X'',
      title_nonce BLOB DEFAULT X'')

wrapped_key(id INTEGER PRIMARY KEY,
            ciphertext BLOB, nonce BLOB, pin_salt BLOB)

meta(key TEXT PRIMARY KEY, value TEXT)
-- Keys: initialized, mnemonic_kdf_salt, account_fingerprint,
--        pin_failed_attempts, pin_lockout_until
```

DB hardening: `PRAGMA secure_delete=ON`, `journal_mode=DELETE`, file permissions `0600`.

---

## Backup Format

File: `PUBKEY_DATE_HHMM.imnotes`

```json
{
  "version": 1,
  "salt": "<base64 mnemonic KDF salt>",
  "nonce": "<base64>",
  "ciphertext": "<base64 AES-256-GCM encrypted JSON array>",
  "noteCount": N
}
```

Decrypted payload: `[{"title": "...", "content": "...", "updatedAt": N}, ...]`

Cross-device restore: same mnemonic + backup's salt → same master key → decrypt.

Backups stored in: `~/.local/share/logos-notes/backups/`

---

## Known Limitations (documented, not bugs)

1. **PIN lockout counter bypassable offline** (#10) — counters in same DB as wrapped key. Offline attacker can reset. Primary defense is Argon2id cost, not the counter.

2. **AAD domain separation not implemented** (#8) — AEAD calls don't bind note ID or schema version. Low priority, future hardening.

---

## What to Review

### Always check

- **Return values:** Every `saveMeta`, `saveNote`, `encrypt`, `decrypt` call should have its return checked. Unchecked returns are the #1 source of silent failures.
- **Mnemonic normalization:** Any code path that touches the mnemonic for crypto must call `normalizeMnemonic()` first.
- **Plugin surface:** New backend methods must be exposed as `Q_INVOKABLE` on `NotesPlugin`.
- **QML syntax:** Run `qmllint` on `plugins/notes_ui/Main.qml`. Logos App silently fails on syntax errors.
- **SecureBuffer usage:** Temporary key material (derived keys, PIN UTF-8, mnemonic UTF-8) should use `SecureBuffer`, not raw `QByteArray`.

### Security-specific

- Nonce reuse (every encrypt must generate a fresh random nonce)
- Key material lifetime (wiped on lock, wiped by SecureBuffer destructor)
- Backup restore path (re-derivation with backup's salt, rollback on failure)
- PIN brute-force (counter persists across restarts, exponential backoff)

### Logos App sandbox

- `ui_qml` plugins can't use `FileDialog`, `StandardPaths`, or `CheckBox` reliably
- All file I/O must go through C++ plugin (`exportBackupAuto`, `listBackups`, `importBackup`)
- QML import paths are restricted — no `Logos.Theme`, no `Logos.Controls`

---

## How to Post Findings

### On GitHub issues

Tag your comments with `Reviewed by: Codex` at the end.

For new findings, create a new issue with:
- Clear title describing the problem
- Labels: `security` or `bug` + `env:` label
- Body: Evidence (file + line), Risk, Recommendation

### On SECURITY_REVIEW.md

You may update `SECURITY_REVIEW.md` directly:
- Add new findings with sequential numbering (#12, #13, etc.)
- Add review round entries to the Review History section
- Mark resolved findings as `✅ RESOLVED`

### Severity levels

| Level | Meaning |
|-------|---------|
| High | Data loss, key exposure, or crypto regression possible |
| Medium | Silent failure, misleading UX, or integrity gap |
| Low | Robustness, future-proofing, code quality |

---

## Communication Protocol

- GitHub issues are the shared channel between you and Claude
- Claude tags as `[Claude Code]`, you tag as `Reviewed by: Codex`
- When you raise a finding, Claude will fix and re-comment
- Re-review after fixes — confirm fixed or note remaining gaps
- Run tests yourself: `cmake --build build && ./build/test_multi_note && ./build/test_security`

---

## File Quick Reference

| What | Where |
|------|-------|
| Security audit | `SECURITY_REVIEW.md` |
| Builder instructions | `CLAUDE.md` |
| This guide | `CODEX.md` |
| Plugin QML (Logos App) | `plugins/notes_ui/Main.qml` |
| Standalone QML | `src/ui/screens/*.qml` |
| Backend core | `src/core/NotesBackend.cpp` |
| Plugin bridge | `src/plugin/NotesPlugin.cpp` |
| Crypto | `src/core/CryptoManager.cpp` |
| Tests | `tests/test_multi_note.cpp`, `tests/test_security.cpp` |
| Blog posts | `blog/` |
| Backup script | `scripts/security-review-loop.sh` |
