Title: Backup import can report successful note restores even when per-note writes fail

Summary
`NotesBackend::importBackup()` increments the imported-note count even if per-note encryption or database persistence fails.

Why this matters
Restore is a recovery path. Reporting success while dropping notes is a data-integrity failure with misleading UX.

Evidence
- `src/core/NotesBackend.cpp`
- In the import loop:
  - `contentCt` and `titleCt` are generated but not validated.
  - `m_db.saveNote(id, contentCt, contentNonce, titleCt, titleNonce);` return value is ignored.
  - `++imported;` executes regardless.

Impact
- The returned JSON can overstate how many notes were restored.
- Users can believe their backup was fully restored when some notes were lost.

Suggested fix
- Check each encryption result and `saveNote()` result before incrementing `imported`.
- On failure, either:
  - abort the whole restore and roll back, or
  - return partial-success status with explicit failure counts.

Suggested tests
- Simulate DB write failure during import and assert the result reports failure or partial success.
- Cover malformed backup entries that cause per-note processing to fail.

---
Reported by: Codex
Role: reviewer
Context: static audit of `master` on 2026-03-15
