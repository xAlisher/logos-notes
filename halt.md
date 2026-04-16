# Halt — 2026-04-16 (session 2)

## Where we stopped

Smoke tested stash-basecamp in Logos Basecamp AppImage. Hit the Nim runtime conflict
(stash statically links libstorage.a, AppImage already has storage_module loading
libstorage.so — two runtimes, stash silently doesn't load). Wrote a cold retrospective
of all storage integration attempts. Discussed QML routing as the next viable path.
User halted to save tokens.

## Current state

- **Branch (stash-basecamp):** `feature/notes-integration` @ `461ebea` — Senty LGTM ✓
- **Branch (logos-notes):** `feature/stash-integration` @ `686beee` — Senty LGTM ✓
- **Build:** both pass ctest (stash 2/2, notes 11/11)
- **AppImage smoke test:** BLOCKED — stash module invisible (Nim runtime conflict)
- **Open review:** none — both branches LGTM, awaiting merge go-ahead from Alisher

## Next steps (in order)

1. **Test QML routing** — cheapest possible experiment:
   - Strip `libstorage.a` linkage from stash CMakeLists.txt (fixes Nim conflict, stash loads)
   - Add one call in stash QML: `logos.callModule("storage_module", "getStatus", [])`
   - Reinstall, launch, check if it returns real data or empty/null
   - If works → tokens available from third-party QML → invest in full rewire
   - If fails → same token wall → go REST HTTP or accept local-only backup

2. **If QML routing works:**
   - Stash C++: add `prepareUpload(moduleName)` → exports file, returns path
   - Stash QML: calls storage_module directly, polls for CID, calls setBackupCid on notes
   - Remove LibStorageTransport + StorageClient from stash (no longer needed)
   - Re-test, Senty review, merge

3. **If QML routing fails:**
   - Decision point: REST HTTP (port 8080) or accept local-only?
   - See `stash-basecamp/docs/why-logos-storage-always-fails.md` for full options

4. **Merge decision (either way):**
   - Alisher still needs to go-ahead on merging both current branches to master
   - Current code is clean and LGTM'd regardless of storage outcome

## Blockers

- Merge go-ahead from Alisher (both branches)
- Decision: QML routing test first, or merge as-is and file follow-up issue?

## Context that's hard to re-derive

- `stash-basecamp/docs/why-logos-storage-always-fails.md` (gitignored) — full
  retrospective on all 3 failed storage attempts, read before starting storage work
- Stash doesn't load in AppImage due to Nim runtime conflict (lesson #41)
- `callModuleParse` canonical form now has inner try/catch — updated in basecamp-skills
- Manifest format fixed (v0.2.0 with `main` map) — stash UI loads, core doesn't
- AppImage path: `~/logos-basecamp-current.AppImage`
- Clean launch sequence in `docs/skills/ecosystem.md` (FUSE unmount before every launch)
- storage_module REST API is on port 8080 — documented in ecosystem.md
