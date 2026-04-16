# Halt — 2026-04-16

## Where we stopped

Built stash-basecamp from scratch — universal module-watch protocol, QML gear panel, uploadWithCallback, auto-poll timer. Senty reviewed twice, LGTM on both stash-basecamp `a3f497c` and logos-notes `f5eceff`. Attempted smoke test in Logos Basecamp AppImage. Hit a fundamental blocker: `stash_plugin.so` statically links `libstorage.a` (Nim runtime) which conflicts with the AppImage's bundled `storage_module_plugin.so` (loads `libstorage.so`). Two Nim runtimes in one process → stash module silently fails to load, not visible in sidebar.

## Current state

- **Branch (stash-basecamp):** `feature/notes-integration` @ `a3f497c` — Senty LGTM ✓
- **Branch (logos-notes):** `feature/stash-integration` @ `f5eceff` — Senty LGTM ✓
- **Build status:** both pass `ctest` (stash 2/2, notes 11/11)
- **AppImage smoke test:** BLOCKED — stash module not visible in sidebar (Nim runtime conflict)
- **Open review:** none — both branches have LGTM, waiting for master merge go-ahead

## Next steps (in order)

1. **Decide: merge current branches as-is or fix first**
   - Current code is correct and tested headlessly. The AppImage conflict is a deployment architecture issue, not a logic bug.
   - Option A: merge now, file issue for LibStorageTransport → IPC rewire in follow-up
   - Option B: rewire first, then merge (significant work — need storage_module API method names)

2. **If merging (Option A):**
   - `cd ~/stash-basecamp && git checkout master && git merge feature/notes-integration`
   - `cd ~/logos-notes && git checkout master && git merge feature/stash-integration`
   - File stash-basecamp issue: "Replace LibStorageTransport with storage_module IPC"
   - Close stash-basecamp #5 (LGTM'd), close logos-notes #95

3. **If rewiring (Option B):**
   - Check storage_module API: what methods does `StorageBackend` expose? (`lm` tool or inspect manifest)
   - Replace `src/core/LibStorageTransport.cpp` with IPC calls via `logosAPI->getClient("storage_module")`
   - Remove `libstorage.a` linkage from `CMakeLists.txt`
   - Re-run tests, re-Senty, re-smoke

4. **Post-merge (whichever option):** update stash-basecamp #6 (Stash Protocol spec) to document the IPC fix path

## Blockers

- Need Alisher's go-ahead to merge both branches to master
- Need Alisher's decision: merge-now or rewire-first

## Context that's hard to re-derive

- The `FooBackend` convention in `checkAll()`: module "notes" → object "NotesBackend". This must match whatever name the peer module registers in `logosAPI->getProvider()->registerObject(...)`.
- `callModuleParse` fix (lesson #44): the inner try/catch is critical. Without it, plain-string returns from `getStatus()` cause every QML refresh to silently fail.
- Manifest format was wrong (old `entry` field) — already fixed, both manifests now at `manifestVersion: 0.2.0`.
- AppImage path: `~/logos-basecamp-current.AppImage` (not `~/logos-app/result/...`)
- FUSE cleanup required before every launch — see `docs/skills/ecosystem.md` launch protocol.
- Lessons #41–#45 written this session, cover all the new failure patterns.
