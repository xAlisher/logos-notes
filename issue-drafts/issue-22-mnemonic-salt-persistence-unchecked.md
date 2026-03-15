Title: Account import succeeds even if mnemonic KDF salt metadata is not persisted

Summary
`NotesBackend::importMnemonic()` does not check whether `mnemonic_kdf_salt` and `account_fingerprint` metadata writes succeed before marking the account initialized and returning success.

Why this matters
Cross-device backup restore depends on the persisted mnemonic KDF salt. If that metadata write fails, `exportBackup()` can later emit a backup with an empty salt and still report success. The result is a latent restore failure that is invisible at creation time.

Evidence
- `src/core/NotesBackend.cpp`
- Wrapped key persistence is checked, but:
  - `m_db.saveMetaBlob("mnemonic_kdf_salt", mnemonicSalt);`
  - `m_db.saveMeta("account_fingerprint", deriveFingerprint(normalized));`
  are not checked.
- The code then calls `m_db.setInitialized()` and proceeds normally.
- `exportBackup()` serializes `m_db.loadMetaBlob("mnemonic_kdf_salt")` without validating non-empty salt.

Impact
- Backups may be generated successfully but be impossible to restore on another device.
- The failure mode is silent and only discovered during disaster recovery.

Suggested fix
- Treat failure to persist either metadata item as fatal during import.
- Roll back the partial account state if either save fails.
- Validate that backup export refuses to proceed if the mnemonic salt is missing.

Suggested tests
- Simulate `saveMetaBlob()` failure and assert import fails.
- Assert `exportBackup()` fails when `mnemonic_kdf_salt` is absent or empty.

---
Reported by: Codex
Role: reviewer
Context: static audit of `master` on 2026-03-15
