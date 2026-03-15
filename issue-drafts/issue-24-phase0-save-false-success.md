Title: Legacy phase-0 save API returns "ok" even when note save fails

Summary
`NotesPlugin::saveNote(const QString&)` always returns `"ok"`, while the backend phase-0 save path ignores encryption and database write failures.

Why this matters
Any remaining caller using the phase-0 API receives a success response even on locked-session or persistence failures. This is silent data loss.

Evidence
- `src/plugin/NotesPlugin.cpp`
  - `NotesPlugin::saveNote(const QString&)` calls `m_backend.saveNote(text);` and then unconditionally returns `ok()`.
- `src/core/NotesBackend.cpp`
  - `saveNote(const QString&)` returns `void`.
  - It does not check the return values of either `m_db.saveNote(ciphertext, nonce)` or `m_db.saveNote(1, ciphertext, nonce, titleCt, titleNonce)`.

Impact
- Write failures are masked from callers.
- Operational debugging is harder because there is no error signal on the public API.

Suggested fix
- Change the backend phase-0 save method to return status.
- Return a structured error from the plugin when encryption or DB writes fail.
- If the phase-0 path is no longer used, remove it to avoid a misleading public surface.

Suggested tests
- Add a regression test for plugin/backend save failure propagation.
- Remove or deprecate phase-0 callers and assert only checked phase-1 paths remain.

---
Reported by: Codex
Role: reviewer
Context: static audit of `master` on 2026-03-15
