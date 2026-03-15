# Security, Privacy, and Cryptography Review

Date: 2026-03-15
Scope: `src/core/*`, `src/plugin/*`, backup import/export flows, and security claims in `README.md`.

## Executive summary

The project has a strong encrypted-at-rest baseline (libsodium Argon2id + AEAD, wrapped master key design), and previously identified P0 issues have been fixed.

Current top residual risks:
1. **PIN lockout is bypassable for offline attackers** because counters are stored in the same editable DB.
2. **Backup portability can silently break** if mnemonic-salt metadata fails to persist during account creation.
3. **Backup restore can report false success** because per-note re-encryption / DB writes are not checked before incrementing the imported count.
4. **AEAD hardening opportunities** remain open: no AAD is used to bind record context.
5. **Legacy phase-0 save API can report success on failed writes**, creating silent data-loss behavior for any remaining callers.

## What is implemented well

- Uses authenticated encryption (`crypto_aead_aes256gcm_encrypt/decrypt`) with random nonce generation for note and wrapped-key encryption.
- Uses Argon2id (`crypto_pwhash(..., ALG_ARGON2ID13)`) for key derivation from mnemonic and PIN.
- Stores only PIN-wrapped master key in DB; mnemonic itself is not persisted.
- Wipes `m_masterKey` on lock/destruction via `sodium_memzero`.

## Findings

## 1) Incomplete mnemonic validation (High) — **Resolved**

**Updated validation:** `KeyManager::isValidMnemonic()` now performs BIP39-style validation: allowed word counts, wordlist membership lookup, checksum verification, and NFKD normalization.

**Status:** No longer a current issue.

**Recommendation:**
- Implement full BIP39 validation (wordlist + checksum).
- Normalize input using BIP39 canonical rules (NFKD).

## 2) Deterministic salt derived from mnemonic (High) — **Resolved**

**Updated validation:** `CryptoManager::randomSalt()` is used for mnemonic KDF, and the salt is persisted via DB metadata (`mnemonic_kdf_salt`).

**Status:** No longer a current issue.

**Recommendation:**
- Use a **random per-account salt** for mnemonic derivation and store it in DB metadata.
- If deterministic re-derivation without storage is a hard requirement, explicitly document reduced resistance to precomputation and increase Argon2 cost materially.

## 3) PIN brute-force resistance is weak (High) — **Partially resolved**

**Updated validation:**
- PIN minimum is 6 (`PIN_MIN_LENGTH = 6`).
- Retry counter + exponential lockout is enforced in `unlockWithPin()` and persisted in DB metadata.
- PIN KDF uses Argon2id `OPSLIMIT_MODERATE/MEMLIMIT_MODERATE`.

**Residual risk:** Offline DB attackers can still reset lockout counters because lockout state is not tamper-resistant (see Known Limitations / Issue #10).

**Recommendation:**
- Raise minimum PIN length and permit/encourage passphrases.
- Use stronger Argon2 profile for PIN wrapping (e.g., sensitive/moderate tuned to UX budget).
- Add local retry backoff / lockout policy for online attempts.

## 4) Plaintext titles in database (Medium) — **Resolved (with legacy caveat)**

**Updated validation:** current save path encrypts titles into `title_ciphertext` + `title_nonce`, clears plaintext title on write, and includes migration logic for legacy plaintext titles.

**Residual caveat:** legacy DB pages may still contain old plaintext title remnants depending on SQLite journaling/secure-delete settings.

**Recommendation:**
- Either encrypt titles (or derive non-sensitive summaries client-side in-memory) OR
- Clearly document that note titles are plaintext metadata.

## 5) Memory zeroization is incomplete (Medium) — ✅ RESOLVED

**Status:** Fixed in `security/p1-fixes` (Issue #6).

Introduced `SecureBuffer` RAII wrapper that calls `sodium_memzero` on destruction.
All temporary key material in `CryptoManager` and `NotesBackend` (mnemonic UTF-8,
PIN UTF-8, derived keys, master key locals) is now wrapped in `SecureBuffer`.

## 6) AES-256-GCM hardware availability not checked (Medium) — ✅ RESOLVED

**Status:** Fixed in `security/p2-fixes` (Issue #7).

**Design decision: fail-fast, no cipher fallback.**

`CryptoManager` constructor checks `crypto_aead_aes256gcm_is_available()`.
If AES-NI is unavailable, `isAvailable()` returns false and `NotesBackend`
shows a clear error to the user and refuses to proceed.

XChaCha20-Poly1305 fallback was considered and rejected because:
- Dual-cipher support creates cipher persistence and cross-device migration
  complexity (confirmed by a Codex review regression finding)
- AES-NI has been standard on x86_64 since Intel Westmere (2010) and
  AMD Bulldozer (2011); ARM equivalent (ARMv8 crypto) covers Apple Silicon
  and modern Android
- The edge case of a CPU without AES-NI in 2026 is near-nonexistent
- Single cipher = simpler code, fewer bugs, no migration paths

## 7) AEAD input validation and domain separation hardening (Low)

**Evidence:** `decrypt()` does not validate nonce length before use; no AAD is used to bind record context (e.g., note ID/version).

**Risk:** Mostly robustness and future-proofing concerns rather than immediate critical exploitability.

**Recommendation:**
- Validate key/nonce sizes before AEAD calls.
- Add AAD binding (record id/schema version) to reduce cross-context misuse risk.

## 8) SQLite privacy hardening opportunities (Low) — ✅ RESOLVED

**Status:** Fixed in `security/p2-fixes` (Issue #9).

- `PRAGMA secure_delete=ON` set on DB open.
- `PRAGMA journal_mode=DELETE` (no WAL residue).
- DB file permissions set to `0600` (owner read/write only).

## 12) Mnemonic KDF salt persistence is not checked during account creation (High)

**Evidence:** `NotesBackend::importMnemonic()` saves the wrapped key first, then calls
`saveMetaBlob("mnemonic_kdf_salt", ...)` and `saveMeta("account_fingerprint", ...)`
without checking either return value. The account is still marked initialized and the
session proceeds as successful even if metadata persistence fails.

**Risk:** Cross-device restore depends on the persisted mnemonic KDF salt. If that write
fails, later backups may serialize an empty salt and still report success, producing
backup files that cannot be restored on another device.

**Recommendation:**
- Treat failure to persist `mnemonic_kdf_salt` as fatal during import.
- Roll back the partially created account if either metadata write fails.
- Add a regression test that simulates metadata write failure before initialization is committed.

## 13) Backup import can claim success while dropping notes (Medium)

**Evidence:** `NotesBackend::importBackup()` increments `imported` even when per-note
content encryption, title encryption, or `m_db.saveNote()` may have failed. Those return
values are currently ignored.

**Risk:** Restore UX can claim data was imported successfully while some notes were never
persisted. This is integrity loss masked as success, which is especially problematic in a
recovery path.

**Recommendation:**
- Check each encrypt/save result before incrementing `imported`.
- Fail the whole restore or at minimum surface partial-failure status to the caller.
- Add tests for malformed backup entries and simulated DB write failures.

## 14) Phase-0 save API returns success even when save fails (Medium)

**Evidence:** `NotesPlugin::saveNote(const QString&)` always returns `"ok"`, while the
backend implementation is `void` and ignores the results of both `m_db.saveNote(...)`
calls.

**Risk:** Any caller still using the phase-0 API can lose writes silently on locked-session,
encryption-failure, or DB-failure paths. Silent success responses make operational issues
hard to detect and debug.

**Recommendation:**
- Change the backend phase-0 save path to return an explicit status.
- Propagate backend errors through the plugin instead of unconditionally returning `"ok"`.
- If phase-0 is dead code, remove the API entirely and keep only the checked phase-1 path.

## ~~Consistency issue in documentation~~ — ✅ RESOLVED

Titles are now encrypted (Issue #5). README updated to match.

## Prioritized remediation plan

1. **P0:** Full BIP39 validation + normalization.
2. **P0:** Replace deterministic mnemonic salt with random persisted salt (or document/tradeoff explicitly and increase KDF cost).
3. **P0:** Strengthen PIN policy + brute-force mitigations + higher Argon2 cost for PIN KDF.
4. **P1:** Eliminate plaintext title leakage or clearly scope claim in README.
5. **P1:** Add secure key-buffer zeroization strategy.
6. **P2:** Add AEAD availability fallback and input validation/AAD hardening.
7. **P2:** SQLite privacy hardening pragmas.

## Known Limitations

### PIN lockout counter is not integrity-protected (Issue #10)

The PIN brute-force counter (`pin_failed_attempts`, `pin_lockout_until`)
is stored in the same SQLite database as the encrypted notes and wrapped
key. There is no HMAC or integrity check on these values.

**Implication:** An offline attacker with direct DB access can reset the
lockout counters before each PIN guess, bypassing the retry throttle
entirely.

**Why this is acceptable for now:**

The lockout counter is a **defense-in-depth** measure against casual and
online attackers (e.g., someone who picks up an unlocked laptop). It is
**not** the primary defense against offline brute-force.

The primary offline defense is the **Argon2id KDF cost**
(`OPSLIMIT_MODERATE` / `MEMLIMIT_MODERATE`), which makes each PIN guess
take ~0.7s on modern hardware with ~256 MB RAM. Combined with the 6+
character PIN minimum, this raises the cost of exhaustive search
significantly.

**Threat model summary:**

| Attacker | Lockout effective? | Primary defense |
|----------|-------------------|-----------------|
| Casual (tries PINs via UI) | Yes | Lockout + Argon2 |
| Offline (copies DB file) | No | Argon2 cost + PIN length |
| Forensic (full disk access) | No | Argon2 cost + PIN length |

**Future hardening (if needed):**

- HMAC the lockout state with a key derived from a platform keychain or
  hardware token (not from the master key — that's circular)
- Move to hardware-backed key storage (Phase 1: Keycard) which eliminates
  offline PIN brute-force entirely

## Review History

### Round 5 — 2026-03-15 (Codex audit of current master)

**Reported by:** Codex
**Role:** reviewer
**Scope:** Current `master` snapshot (`src/core/*`, `src/plugin/*`, backup import/export paths, tests)
**Branch/commit:** local `master` checkout

**Findings (3 total):**

| # | Severity | Issue | Summary |
|---|----------|-------|---------|
| #12 | High | Mnemonic salt persistence unchecked | Account import succeeds even if `mnemonic_kdf_salt` metadata write fails, which can later produce unrestorable backups |
| #13 | Medium | Backup import false-success accounting | Restore increments imported count without checking encrypt/save results for each note |
| #14 | Medium | Phase-0 save API false success | Plugin `saveNote(text)` always returns `"ok"` even if backend write fails |

**Confirmed still open:**
- #8 AAD/domain separation remains future hardening work.
- #10 offline lockout counter reset remains a documented limitation.

**Testing gap noted:**
- Existing tests do not exercise backup export/import or metadata-persistence failure paths.

### Round 3 — 2026-03-13 (Codex re-validation)

**Reviewer:** OpenAI Codex
**Scope:** `SECURITY_REVIEW.md` claim validation against current `src/core/*` and tests
**Branch:** current working branch

**Validation outcome:**

- Confirmed fixed: #2 (PIN hardening baseline), #3 (BIP39 validation), #4 (random mnemonic salt), #5 (encrypted titles in current write path).
- Confirmed still open: #6 (temporary key copies may persist), #7 (AES-GCM availability fallback), #8 (nonce/AAD hardening), #9 (SQLite privacy pragmas).
- Confirmed known limitation remains: #10 (offline lockout counter reset).

This document has been updated to reflect current status so unresolved risks are not mixed with already remediated findings.

### Round 1 — 2026-03-13 (AI review by Claude Code)

**Reviewer:** Claude Code (Opus)
**Scope:** Full codebase — `src/core/`, `src/ui/`, database schema, encryption architecture
**Branch:** `master` (pre-security-fixes)

**Findings (8 total):**

| # | Severity | Issue | Summary |
|---|----------|-------|---------|
| #2 | P0 | PIN brute-force resistance | No retry counter or lockout; 4-digit PIN minimum; Argon2 OPSLIMIT_INTERACTIVE too weak |
| #3 | P0 | BIP39 wordlist validation | isValidMnemonic() only checked word count, no wordlist or checksum verification |
| #4 | P0 | Deterministic mnemonic salt | SHA-256(mnemonic) as Argon2 salt enables precomputation attacks |
| #5 | P1 | Plaintext note titles | `notes.title` stored unencrypted in SQLite, contradicts "no plaintext on disk" claim |
| #6 | P1 | Temporary key buffers not zeroed | QByteArray locals holding key material not wiped after use |
| #7 | P1 | AES-256-GCM hardware check missing | No `crypto_aead_aes256gcm_is_available()` call; silent failure on CPUs without AES-NI |
| #8 | P2 | AEAD input validation / AAD | No nonce length validation; no AAD for domain separation |
| #9 | P2 | SQLite privacy hardening | No `PRAGMA secure_delete`; default journal mode; no file permission enforcement |

**P0 issues fixed on `security/p0-fixes` branch. P1/P2 issues remain open.**

---

### Round 2 — 2026-03-13 (Codex review of P0 fixes)

**Reviewer:** OpenAI Codex (via `scripts/security-review-loop.sh`)
**Scope:** `src/core/` diff on `security/p0-fixes` branch (commits `b18df1d`..`c009e88`)
**Branch:** `security/p0-fixes`

**Findings (2 total):**

| # | Severity | Issue | Summary |
|---|----------|-------|---------|
| #10 | P1 | PIN lockout state bypassable | Lockout counters in same DB as wrapped key; offline attacker can reset before each guess |
| #11 | P2 | saveMeta() return unchecked | Failed counter persistence silently ignored; restart resets attempt count |

**Confirmed correct:**
- Random salt generation and persistence (Issue #4 fix)
- BIP39 wordlist + checksum validation (Issue #3 fix)
- Argon2id cost upgrade to OPSLIMIT_MODERATE
- Brute-force backoff logic (exponential schedule)

**No critical crypto regressions found.**

Both findings addressed: #11 fixed (return value checks + warnings), #10 documented as known limitation (see above).

---

### Round 4 — 2026-03-14 (Codex review of P2 fixes)

**Reviewer:** OpenAI Codex
**Scope:** `src/core/` diff on `security/p2-fixes` branch
**Branch:** `security/p2-fixes`

**Findings (2 total):**

| Severity | Finding | Resolution |
|----------|---------|------------|
| High | Cipher choice not persisted — AES-GCM data unreadable on XChaCha20 machine | XChaCha20 fallback stripped entirely; fail-fast AES-NI check only. Design decision documented. |
| Medium | Nonce length not validated before AEAD calls | Added `nonce.size() != NONCE_BYTES` check in `decrypt()`, returns empty on mismatch |

**Design decision:** XChaCha20-Poly1305 fallback rejected in favor of fail-fast AES-NI
requirement. Dual-cipher complexity created the persistence regression. AES-NI is
universal on x86_64 since 2010 and ARM since ARMv8.

**Confirmed fixed in this round:** #7 (AES-NI check), #9 (SQLite hardening),
nonce validation (partial #8).

**All P0, P1, P2 issues now resolved or documented.** Only #8 AAD domain separation remains open (low priority, future hardening).
