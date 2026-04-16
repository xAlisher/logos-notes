# Ecosystem Reference

> Reference for cross-project work, storage integration, and external dependencies.

---

## Logos Basecamp Launch Protocol

AppImage FUSE mounts accumulate silently across runs and never self-clean when processes are killed hard.
After dozens of sessions there will be hundreds of stale `/tmp/.mount_logos-*` entries. They don't
block launch outright but cause subtle module-load failures and confusing log noise.

**Always run this sequence before launching:**

```bash
# 1. Kill all Logos processes
pkill -9 -f "logos_host.elf"; pkill -9 -f "LogosBasecamp.elf"; pkill -9 -f "logos-basecamp"

# 2. Unmount stale FUSE mounts
mount | grep "fuse.logos" | awk '{print $3}' | xargs -I{} fusermount -u {} 2>/dev/null

# 3. Verify clean (both must return nothing / 0)
mount | grep "fuse.logos" | wc -l
ps aux | grep -E "logos_host|logos-basecamp" | grep -v grep

# 4. Launch
nohup ~/logos-basecamp-current.AppImage > /tmp/basecamp.log 2>&1 &
```

**Why:** `pkill -9` kills the process without giving AppImage a chance to unmount its FUSE loop device.
Repeated kills → hundreds of ghost mounts → disk space waste, and occasionally inode exhaustion on `/tmp`.

**pkill chaining rule:** Never chain `pkill` with `&&`. It exits 1 when nothing matches, aborting the chain.
Always use `;` between kill commands. Use `2>/dev/null; true` if you need zero exit for downstream commands.

**AppImage path:** `~/logos-basecamp-current.AppImage` — the canonical pinned test binary.
Never construct from `~/logos-app/result/` — that Nix result symlink is garbage-collected.

**run_in_background:** Never use `run_in_background: true` to launch the AppImage. The AppImage
daemonizes and the wrapper exits non-zero, reporting false failure. Use `nohup ... > /tmp/basecamp.log 2>&1 &`.

---

## Logos Module Manifest Format (current — v0.2.0)

Always use the notes manifests as canonical reference. The old format (`"entry"` field, no `manifestVersion`) causes silent load failure.

**Core module manifest** (`modules/<name>/manifest.json`):
```json
{
  "author": "...",
  "category": "...",
  "dependencies": [],
  "description": "...",
  "icon": "",
  "main": {
    "linux-amd64":       "foo_plugin.so",
    "linux-amd64-dev":   "foo_plugin.so",
    "linux-x86_64-dev":  "foo_plugin.so",
    "darwin-arm64":      "foo_plugin.dylib"
  },
  "manifestVersion": "0.2.0",
  "name": "foo",
  "type": "core",
  "version": "1.0.0"
}
```

**UI plugin manifest** (`plugins/<name>_ui/manifest.json`):
```json
{
  "author": "...",
  "category": "...",
  "dependencies": ["foo"],
  "description": "...",
  "icon": "",
  "main": {},
  "manifestVersion": "0.2.0",
  "name": "foo_ui",
  "type": "ui_qml",
  "version": "1.0.0",
  "view": "Main.qml"
}
```

---

## CMake install(CODE) Escaping

In `install(CODE "...")` blocks, variable expansion rules differ from normal CMake:

| Form | Result |
|------|--------|
| `\${var}` | Deferred — expands to `${var}` at install time ✓ |
| `\\${var}` | **Wrong** — `\\` → literal `\`, `${var}` evaluated at configure time (empty) → parse error |
| `${VAR}` | Configure-time — correct for CMake variables known at configure time (paths, executables) |

**Rule:** Use `\${var}` for variables set inside the install script. Use `${VAR}` for CMake variables from configure time. Reference: `logos-notes/CMakeLists.txt` install blocks.

---

## Stash Architecture — Known Limitation

`stash_plugin.so` statically links `libstorage.a` (Nim runtime). The AppImage bundles `storage_module_plugin.so` which dynamically loads `libstorage.so`. Two Nim runtimes in the same process = stash module silently fails to load (no error in log).

**Fix path (Phase 2):** Replace `LibStorageTransport` with IPC calls to `storage_module`:
```cpp
auto* client = logosAPI->getClient("storage_module");
client->invokeRemoteMethod("StorageBackend", "uploadFile", filePath);
```
Stash is an orchestrator, not a storage node. It should use the existing node, not embed one.

---

## Logos Developer Tools

| Tool | Purpose |
|------|---------|
| `lm` | Inspect compiled module binaries (metadata + method signatures) |
| `logoscore` | Headless runtime — load modules and invoke methods from CLI without logos-app |
| `lgpm` | Package manager CLI — install/list LGX packages |
| `logos-cpp-generator` | Generate typed SDK wrappers from compiled modules |
| `mkLogosModule` | Nix function — builds core modules from `module.yaml` |

---

## Logos Storage Research Notes

### Architecture
```
libstorage.so (Nim, ~14MB) — Logos Storage node (formerly Codex)
storage_module_plugin.so — Qt C++ wrapper
storage_ui.so — QML UI plugin
```

### Network Ports
| Port | Protocol | Service |
|------|----------|---------|
| 8500 | TCP | libp2p transport |
| 8090 | UDP | DiscoveryV5 peer discovery |
| 8080 | TCP | REST API |

### REST API
```
POST /api/storage/v1/data          — upload file → CID
GET  /api/storage/v1/data/{cid}    — download
POST /api/storage/v1/data/{cid}/network — fetch from network
GET  /api/storage/v1/debug/info    — node status
```

### Key Findings (tested 2026-03-12)
- Upload → CID ✅, fetch while online ✅
- Blobs stored locally only — no auto-replication to peers
- Delete = gone permanently if no other node has fetched it
- Single device limitation: full sync requires minimum 2 nodes
- Works on cellular with `nat: "none"` in config
- **Storage team confirmed**: web-of-trust peer storage is intended usage

### Integration Path for Phase 2
- Notes module calls storage_module via `LogosAPIClient::invokeRemoteMethod()`
- Upload: `uploadInit(filepath, chunkSize)` → `uploadChunk()` → `uploadFinalize()` → CID
- Download: `downloadToUrl(cid, destUrl, local, chunkSize)` → file at destination
- All operations async via `eventResponse` signal

---

## Import Path Vision

Three ways to derive the same 256-bit AES-256-GCM master key:

```
Import Screen:
  ┌─ 1. Enter Recovery Phrase ──→ Argon2id → master key (implemented, always available)
  ├─ 2. Connect Keycard ────────→ keycard-basecamp module → master key (implemented v1.0.0)
  └─ 3. Connect Logos Wallet ───→ accounts_module derives key → master key (planned, #32)
```

Same DB, same encryption, same notes. User picks their preferred key management.
`key_source` meta field tracks which path was used (`"mnemonic"` or `"keycard"`).
Switching between methods: re-encrypt via backup/restore flow.

### Keycard Integration (current — v1.2.0+)

Keycard support is provided by the **keycard-basecamp module** (external, separate repo).
Notes does not bundle any keycard libraries or PC/SC code.

**Architecture:**
- QML calls `logos.callModule("keycard", "requestAuth", ["notes_encryption", "notes"])` to request key derivation
- Polls `logos.callModule("keycard", "checkAuthStatus", [authId])` until complete
- Receives hex-encoded 256-bit key, passes to `importWithKeycardKey()` / `unlockWithKeycardKey()`
- No `wrapped_key` stored — card re-derives key on every unlock
- Fingerprint derived from `SHA-256(master_key) → Ed25519 seed → public key`

> **Historical (pre-v1.2.0):** Keycard integration originally used an internal `KeycardBridge`
> wrapping `libkeycard.so` (Go `status-keycard-go` compiled as shared library) with JSON-RPC
> over C API (`KeycardInitializeRPC`, `KeycardCallRPC`, `KeycardSetSignalEventCallback`).
> The runtime no longer uses any of this since Epic #62. All legacy artifacts have been
> removed from the repo.

### Wallet Integration (v0.7.0+)

- `accounts_module` wraps `go-wallet-sdk` — provides `createRandomMnemonic()`, HD keystore, PIN management
- Lightweight option: "Generate with Wallet" button on import screen (~10 lines QML)
- Full option: wallet holds the key, notes requests it via LogosAPI

---

## Phase 3 — Trust Network (Proposal)

**Concept**: small group of trusted friends store each other's encrypted backups. No central server.

**Flow**:
1. App auto-uploads encrypted backup to local Logos Storage node → CID
2. Settings → Trust Network: paste list of trusted public keys
3. App sends CID to trusted peers via Logos Messaging
4. Peer apps fetch + pin the blob if sender is in their trust list
5. Mutual by design — both sides must add each other

**Dashboard**:
```
Storing for me:    3 / 5 nodes online
I'm storing for:   4 identities
Last backup:       2 min ago
Backup CID:        zDvZ...  [Copy]
```

**Abuse prevention**: per-peer storage limit, revoke trust instantly, block key.

---

## Blog Posts

| File | Topic | Status |
|------|-------|--------|
| `blog/2026-03-12-phase-0.md` | Phase 0 — module, crypto architecture | Submitted to Logos press, pending publication |
| `blog/2026-03-14-security-hardening.md` | Security hardening, two-AI review loop | Published |
| `blog/2026-03-14-settings-backup-identity.md` | Settings, backup, identity/fingerprint | Published |
| `blog/2026-03-15-hotfixes-and-sandbox-lessons.md` | Hotfixes, QML sandbox lessons | Published |
| `blog/2026-03-15-shared-memory.md` | Shared memory, knowledge/instructions split, agent collaboration | Draft |
| `blog/2026-03-15-building-immutable-notes-on-logos.md` | Comprehensive Logos blog post — app, crypto, module architecture, lessons | Ready for Logos submission |

---

## Local Clone Paths

| Repo | Local path |
|------|-----------|
| logos-app (Logos Basecamp, built AppImage runs) | `~/logos-app/` |
| status-desktop (QML/Nim reference) | `~/status-desktop/` |

---

## Key Links

| Resource | URL |
|----------|-----|
| Ideas issue | https://github.com/logos-co/ideas/issues/13 |
| Project repo | https://github.com/xAlisher/logos-notes |
| Logos Basecamp (app) | https://github.com/logos-co/logos-app |
| Logos App PoC (legacy) | https://github.com/logos-co/logos-app-poc |
| Chat UI reference | https://github.com/logos-co/logos-chat-ui |
| Chat module reference | https://github.com/logos-co/logos-chat-module |
| Template module | https://github.com/logos-co/logos-template-module |
| Developer tutorial | https://github.com/logos-co/logos-tutorial |
| C++ SDK | https://github.com/logos-co/logos-cpp-sdk |
| Logos docs | https://github.com/logos-co/logos-docs |
| Logos Storage | https://github.com/logos-storage/logos-storage-nim |
| Logos Messaging | https://github.com/logos-messaging/nim-chat-poc |
| Waku docs | https://docs.waku.org |
| Design system | https://github.com/logos-co/logos-design-system |
| nix-bundle-lgx | https://github.com/logos-co/nix-bundle-lgx |
| Package Manager module | https://github.com/logos-co/logos-package-manager-module |
| Status contact | https://status.app/u/CwmAChEKD0FsaXNoZXIgU2hlcmFsaQM= |
