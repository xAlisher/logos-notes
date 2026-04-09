# v2.0 — Disaster Recovery via Logos Storage

> **Status:** Planning (revised after Senty review on issue #75)
> **Target:** v2.0.0
> **Depends on:** storage_module installed in Logos Basecamp

## Use Cases

v2.0 addresses **one** problem only: same-device disaster recovery.

| # | Use case | Phase | Status |
|---|----------|-------|--------|
| 1 | **Same-device disaster recovery (Keycard)** — user reinstalls module, loses local DB, or recovers after failure. Still has their Keycard. Taps card → derives same master key → decrypts backup blob from CID or file. | **v2.0** | This plan |
| 2 | Cross-device recovery — user installs on a new device, needs to find their "latest backup" CID without prior knowledge | Future | Needs CID discovery layer (separate problem) |
| 3 | Trust-network replication — peers store each other's encrypted snapshots, signed CID announcements | v3.0 | Out of scope |
| 4 | Live cross-device note sync — ordering, freshness, offline edits, conflicts | Not committed | Likely never; snapshot replication may be enough |

**Boundary discipline:** v2.0 is backup + recovery. Not sync. Not discovery. Not replication.

**Key path:** v2.0 disaster recovery is **Keycard-only**. The mnemonic+PIN path remains in the app but is not a target of v2.0 auto-backup or restore-from-CID.

**No backwards compatibility constraint:** This is POC / experimental software with no production users. We do not preserve old method names, do not migrate state, do not maintain dual code paths "for old users". When something needs to change, we change it.

## Goal

Automatically back up encrypted notes to the local Logos Storage node after edits,
track the CID locally, allow restore from a backup file or a manually-supplied CID.
Restore is a **true restore**: wipe existing DB state, then import. Not append.

## Architecture

```
saveNote() ──→ debounce (30s) ──→ exportBackupAuto()
                                      │
                                      ▼
                              encrypted .json blob
                                      │
                                      ▼
                          StorageClient::upload(blob)
                              (invokeRemoteMethod)
                                      │
                                      ▼
                              storage_module uploads
                                      │
                                      ▼
                              CID returned via eventResponse
                                      │
                                      ▼
                              meta["backup_cid"] = CID
                              meta["backup_timestamp"] = now
                              (local-only knowledge of "my latest CID")

Restore from CID (Keycard, same device):
  Import screen / Settings → "Restore from Storage"
      │
      ▼
  Tap Keycard → keycard-basecamp module derives 256-bit master key
      │
      ▼
  StorageClient::download(cid) → temp file
      │
      ▼
  restoreFromFileWithKeycard(tempFile, hexKey) ──→ TRUE RESTORE:
                                                   1. Decrypt + validate blob
                                                   2. Wipe existing notes table
                                                   3. Insert restored notes
                                                   4. Atomic — rollback on any failure
```

Keycard derivation is deterministic: same card always produces the same master key for the `("notes_encryption", "notes")` context. The new install can therefore decrypt a blob the old install made, with no shared state beyond the card itself.

## Restore Contract (v2.0)

**A restore is destructive and atomic.** It is a brand-new method, separate from the existing `importBackup()` (which is append-style and only used in account-import-with-backup-file flows where the destination DB is freshly initialized and empty).

| Step | Behavior |
|------|---------|
| 1. Validate | Blob decrypts, JSON parses, version recognized. Fail-closed if any check fails. |
| 2. Stage | Insert decrypted notes into a temp table or in-memory list. |
| 3. Wipe | Delete all rows from `notes` table. |
| 4. Commit | Insert staged notes into `notes`. |
| 5. Rollback | If any step fails, restore previous state. Never leave half-state. |

**Why destructive:** Same-device disaster recovery means "my DB is gone or wrong; replace it with this snapshot." Append semantics produce duplicates and silent corruption.

The existing `importBackup()` stays as-is. It's only called from `importMnemonic(..., backupPath)` and `importWithKeycardKey(..., backupPath)` — both flows where the DB is empty, so the append behavior is harmless. v2.0 disaster recovery does not use it.

## Integration Path

- Notes calls `storage_module` via `LogosAPIClient::invokeRemoteMethod()`
- Upload: `uploadInit(filepath, chunkSize)` → `uploadChunk()` → `uploadFinalize()` → CID
- Download: `downloadToUrl(cid, destUrl, local, chunkSize)` → file at destination
- All operations async via `eventResponse` signal
- Source: `docs/skills/ecosystem.md` storage research (tested 2026-03-12)

## Phases

### Phase 1: Storage Service Client (foundation)
**Use case:** All v2.0 use cases depend on this.
- C++ `StorageClient` class wrapping `storage_module` IPC
- Service availability check (graceful degradation if not installed)
- Upload/download abstraction over chunked API
- Async signal flow: request → eventResponse → callback
- Unit tests with mock IPC

### Phase 2: Auto-Backup on Save
**Use case:** Same-device disaster recovery — produce snapshots the user can recover from later.
- Debounce timer (30s after last saveNote)
- Trigger `exportBackupAuto()` → upload via StorageClient
- Store CID + timestamp in meta table (local-only knowledge)
- `getBackupCid()` Q_INVOKABLE on NotesPlugin
- Handle upload failures gracefully (log, retry next save)

### Phase 3: True Restore Contract (DB-destructive, Keycard)
**Use case:** Same-device disaster recovery (Keycard) — replace current DB with a snapshot using the master key derived from the card.
- New `restoreFromFileWithKeycard(filePath, hexKey)` on NotesBackend — implements the restore contract above
- New `restoreFromCidWithKeycard(cid, hexKey)` — download blob → call `restoreFromFileWithKeycard`
- Both Q_INVOKABLE on NotesPlugin
- Atomic semantics: stage → wipe → commit, with rollback on any failure
- Existing `importBackup()` is left untouched. No rename. No caller changes.
- Tests:
  - Restore into empty DB (Keycard key)
  - Restore into populated DB (verify wipe + replace, no duplicates)
  - Restore failure mid-way → previous state preserved
  - Wrong key (different card) → fail-closed, DB untouched
  - Corrupt blob → fail-closed, DB untouched
  - End-to-end: blob created on install A, restored on install B with same card

### Phase 4a: Settings UI — backup status display
**Use case:** Visibility for the user — "is my backup working? when was the last one?"
- Depends on Phase 2 only
- New "Backup" section in Settings: CID, timestamp, status indicator, "Back up now" button
- Display only — no destructive flows here

### Phase 4b: Restore-from-Storage UI flow
**Use case:** Same-device disaster recovery — recover notes by tapping Keycard + CID.
- Depends on Phase 3 only
- "Restore from Storage" entry on import screen and in Settings
- Flow: tap Keycard → derive key → enter or auto-fill CID → destructive warning dialog → restore
- No mnemonic field anywhere

## What v2.0 explicitly does NOT do

- ❌ **Cross-device "latest CID" lookup** — there is no discovery layer. If you install on a new device, you must already know your CID. This is a separate future problem.
- ❌ **Peer replication / Trust Network** — v3.0 scope.
- ❌ **Signed CID announcements** — needs identity-keyed registry, future work.
- ❌ **Live note sync** — out of scope, possibly out of project entirely.
- ❌ **Schema migrations** — uses existing meta key-value table.

## Risks

| Risk | Mitigation |
|------|-----------|
| storage_module not installed | Check availability at init, disable auto-backup, show "Storage not available" in UI |
| Upload blocks UI thread | All IPC is async via eventResponse — verify with large backups |
| Large backup blobs | Per Giuliano: one blob per backup is correct. Test with 100+ notes |
| Storage node offline | Queue backup, retry on next save. CID stays stale until upload succeeds |
| Stale CID after failed upload | Only update backup_cid after confirmed upload success |
| User restores into populated DB and loses current work | UI warning before destructive restore, plus auto-backup-before-restore safeguard |
| Restore mid-failure leaves half-state | Atomic transaction: stage → wipe → commit, rollback on any step failure |

## Decisions

- **Disaster recovery only** — not sync, not discovery, not replication
- **One blob per backup** (per Giuliano)
- **Restore is destructive and atomic** — not the same as current `importBackup()`
- **CID is local-only knowledge in v2.0** — no cross-device "latest CID" lookup
- **Debounce, not real-time** — 30s after last save
- **Graceful degradation** — app works fully without storage_module

## New Q_INVOKABLE Methods

```cpp
// Backup status
Q_INVOKABLE QString getBackupCid();           // {"cid":"...", "timestamp":"..."}
Q_INVOKABLE QString getStorageStatus();       // available/unavailable/uploading/synced/failed
Q_INVOKABLE QString triggerBackup();          // manual "back up now"

// Restore (v2.0 — Keycard, destructive, atomic)
Q_INVOKABLE QString restoreFromFileWithKeycard(const QString& filePath, const QString& hexKey);
Q_INVOKABLE QString restoreFromCidWithKeycard(const QString& cid, const QString& hexKey);

// Existing methods stay untouched:
// - importBackup() — append-style, only used from account-import-with-backup flows (empty DB)
```

## Schema Changes

None. `backup_cid` meta key already exists. Add `backup_timestamp` and
`storage_status` as new meta keys (no migration needed, meta is key-value).

