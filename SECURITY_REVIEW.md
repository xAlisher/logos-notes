# Security, Privacy, and Cryptography Review

Date: 2026-03-13
Scope: `src/core/*` and security claims in `README.md`.

## Executive summary

The project has a solid encrypted-at-rest foundation (libsodium Argon2id + AEAD, wrapped master key design), but there are several high-impact gaps that should be addressed before positioning it as strongly secure against local/offline attackers.

Top risks:
1. **Weak mnemonic validation** (not true BIP39 validation) can silently accept invalid phrases.
2. **Deterministic salt from mnemonic** enables cross-user precomputation attacks for common mnemonic inputs.
3. **PIN-unlock hardening is incomplete** (no retry throttling/lockout; interactive Argon2 profile only).
4. **Data minimization/privacy mismatch**: note titles are stored plaintext while docs claim no plaintext on disk.
5. **Memory hygiene gaps**: derived keys and temporary key material are not consistently zeroized.

## What is implemented well

- Uses authenticated encryption (`crypto_aead_aes256gcm_encrypt/decrypt`) with random nonce generation for note and wrapped-key encryption.
- Uses Argon2id (`crypto_pwhash(..., ALG_ARGON2ID13)`) for key derivation from mnemonic and PIN.
- Stores only PIN-wrapped master key in DB; mnemonic itself is not persisted.
- Wipes `m_masterKey` on lock/destruction via `sodium_memzero`.

## Findings

## 1) Incomplete mnemonic validation (High)

**Evidence:** `KeyManager::isValidMnemonic()` only checks 12/24 words and that the first char of each token is alphabetic, without BIP39 wordlist membership or checksum verification.

**Risk:** User can import invalid recovery phrase text and still derive/store a key, causing possible irrecoverable data loss and false security assumptions.

**Recommendation:**
- Implement full BIP39 validation (wordlist + checksum).
- Normalize input using BIP39 canonical rules (NFKD).

## 2) Deterministic salt derived from mnemonic (High)

**Evidence:** `CryptoManager::saltFromMnemonic()` returns `SHA-256(mnemonic)` and `deriveKey()` uses it as Argon2 salt.

**Risk:** This creates deterministic KDF inputs per mnemonic, making large-scale precomputation/rainbow-style optimizations possible for common/weak phrase patterns. A random persisted salt is safer for password-style KDF workflows.

**Recommendation:**
- Use a **random per-account salt** for mnemonic derivation and store it in DB metadata.
- If deterministic re-derivation without storage is a hard requirement, explicitly document reduced resistance to precomputation and increase Argon2 cost materially.

## 3) PIN brute-force resistance is weak (High)

**Evidence:**
- PIN minimum is 4 (`PIN_MIN_LENGTH = 4`).
- No retry counter, delay, or lockout is enforced in `unlockWithPin()`.
- Argon2 uses `OPSLIMIT_INTERACTIVE/MEMLIMIT_INTERACTIVE` for PIN KDF.

**Risk:** Offline DB attackers can attempt high-rate PIN guesses, especially if users choose short numeric PINs.

**Recommendation:**
- Raise minimum PIN length and permit/encourage passphrases.
- Use stronger Argon2 profile for PIN wrapping (e.g., sensitive/moderate tuned to UX budget).
- Add local retry backoff / lockout policy for online attempts.

## 4) Plaintext titles in database (Medium)

**Evidence:** `notes.title` is stored as plaintext and filled from first plaintext line before DB write.

**Risk:** Leaks sensitive note content metadata even when note body is encrypted. Also conflicts with README claim of "no plaintext on disk."

**Recommendation:**
- Either encrypt titles (or derive non-sensitive summaries client-side in-memory) OR
- Clearly document that note titles are plaintext metadata.

## 5) Memory zeroization is incomplete (Medium)

**Evidence:**
- `m_masterKey` is wiped on lock, but temporary key buffers (`pinKey`, `masterKey` locals, derived keys in `CryptoManager`) are returned/copied as ordinary `QByteArray` and not explicitly cleared after use.

**Risk:** Sensitive material may persist in heap pages longer than needed.

**Recommendation:**
- Introduce secure buffer lifecycle helpers and explicit `sodium_memzero` for temporary secret arrays after final use.
- Minimize copies of key material (move semantics / dedicated secure container where possible).

## 6) AES-256-GCM hardware availability not checked (Medium)

**Evidence:** Code directly uses `crypto_aead_aes256gcm_*` APIs without checking `crypto_aead_aes256gcm_is_available()`.

**Risk:** On CPUs lacking AES acceleration, API may be unavailable and encryption/decryption will fail unexpectedly.

**Recommendation:**
- Check availability at startup; fallback to `crypto_aead_xchacha20poly1305_ietf` when unavailable.

## 7) AEAD input validation and domain separation hardening (Low)

**Evidence:** `decrypt()` does not validate nonce length before use; no AAD is used to bind record context (e.g., note ID/version).

**Risk:** Mostly robustness and future-proofing concerns rather than immediate critical exploitability.

**Recommendation:**
- Validate key/nonce sizes before AEAD calls.
- Add AAD binding (record id/schema version) to reduce cross-context misuse risk.

## 8) SQLite privacy hardening opportunities (Low)

**Evidence:** Standard SQLite usage; no explicit PRAGMA hardening for secure deletion/journal handling.

**Risk:** Deleted plaintext metadata (like titles) may remain in freelists/WAL; forensic recoverability increases.

**Recommendation:**
- Consider `PRAGMA secure_delete=ON`.
- Define WAL/journal policy aligned with threat model.
- Revisit DB file permissions and backup/export paths.

## Consistency issue in documentation

README states "no plaintext on disk," but title metadata is currently plaintext in `notes.title`. Update docs or implementation to match.

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
