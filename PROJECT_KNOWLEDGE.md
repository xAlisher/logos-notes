# Immutable Notes — Project Knowledge
*Last updated: 2026-03-19*

> **This file is the project's shared memory.**
> It lives in the repo root and is committed like any other file.
> GitHub issues are ephemeral. This file is not.

---

## What We're Building

Encrypted, local-first Markdown notes app for the Logos ecosystem.

- **Approved idea**: https://github.com/logos-co/ideas/issues/13
- **Repo**: https://github.com/xAlisher/logos-notes
- **Stack**: C++17, Qt 6.9.3, QML, libsodium 1.0.18, SQLite, CMake + Nix
- **OS**: Ubuntu 24.04

### Vision
Encrypted notes with Keycard hardware key protection, synced across devices via Logos Messaging and Logos Storage. No accounts. No servers. Your recovery phrase is your identity.

---

## Current Phase: v1.1.0 released — production ready with reproducible builds

### Completed phases

| Version | What shipped |
|---------|-------------|
| v0.1.0 | Phase 0 — single note, module registration |
| v0.2.0 | Phase 1 — multiple notes, sidebar |
| v0.3.0 | Security hardening (P0+P1) |
| v0.4.0 | P2 security fixes, AES-NI fail-fast |
| v0.5.0 | Settings, backup export/import, stable identity |
| v0.6.0 | LGX packaging, 95-case test suite (backup, account, note API, plugin) |
| v1.0.0 | Keycard hardware key derivation + UI polish |
| v1.1.0 | Build libkeycard from source + portable LGX packaging (issue #44) |

### Roadmap

| Version | Description | Status |
|---------|-------------|--------|
| v0.6.0 | LGX package for Logos App Package Manager | ✅ Complete |
| v0.6.0 | AppImage standalone installer | Parked — blocked on Qt QML AOT |
| v1.0.0 | Keycard hardware key derivation + UI polish | ✅ Complete |
| v1.1.0 | Build libkeycard from source + portable LGX packaging | ✅ Released — issue #44 complete |
| v2.0 | Logos Storage auto-backup + CID tracking | Research |
| v3.0 | Trust Network — social backup via web of trust | Proposal stage |

---

## Architecture

### Encryption
```
BIP39 mnemonic
  → NFKD normalize
  → Argon2id (random persisted salt, OPSLIMIT_MODERATE)
  → 256-bit master key (never stored)

PIN
  → Argon2id (random salt, OPSLIMIT_MODERATE)
  → PIN wrapping key
  → AES-256-GCM(master key) → wrapped key blob stored in SQLite

Note content + title
  → AES-256-GCM(plaintext, master key, random nonce)
  → ciphertext + nonce stored in SQLite
  → plaintext never touches disk

Account identity
  → SHA-256(normalized_mnemonic) → Ed25519 seed
  → crypto_sign_seed_keypair() → public key
  → first 16 bytes hex = account fingerprint
  → deterministic, stable across devices and re-imports
```

### SQLite schema
```sql
notes(id, ciphertext, nonce, title_ciphertext, title_nonce, updated_at)
wrapped_key(id, ciphertext, nonce, pin_salt)
meta(key, value)
-- meta keys: initialized, mnemonic_kdf_salt, account_fingerprint,
--            pin_failed_attempts, pin_lockout_until, backup_cid
```

### DB paths
| Context | Path |
|---------|------|
| Logos Basecamp plugin | `~/.local/share/logos_host/notes.db` |
| Standalone app | `~/.local/share/logos-co/logos-notes/notes.db` |

When wiping for tests, delete both. When verifying DB contents, check which context you're in.

### Logos Basecamp module structure
```
# Portable builds (AppImage/LGX)
~/.local/share/Logos/LogosBasecamp/modules/notes/
├── manifest.json          (type: "core")
└── notes_plugin.so

~/.local/share/Logos/LogosBasecamp/plugins/notes_ui/
├── manifest.json          (type: "ui_qml")
├── metadata.json
└── Main.qml

# Dev builds (cmake install)
~/.local/share/Logos/LogosBasecampDev/modules/notes/
~/.local/share/Logos/LogosBasecampDev/plugins/notes_ui/
```

### Plugin contract (Q_INVOKABLE surface)
```cpp
Q_INVOKABLE void initLogos(LogosAPI* api);
Q_INVOKABLE bool initialize();
Q_INVOKABLE QString isInitialized();
Q_INVOKABLE QString importMnemonic(QString mnemonic, QString pin, QString confirm, QString backupPath);
Q_INVOKABLE QString unlockWithPin(QString pin);
Q_INVOKABLE QString lockSession();
Q_INVOKABLE QString resetAndWipe();
Q_INVOKABLE QString getAccountFingerprint();
Q_INVOKABLE QString exportBackup();
Q_INVOKABLE QString listBackups();
Q_INVOKABLE QString importBackup(QString path);
Q_INVOKABLE QString createNote();
Q_INVOKABLE QString loadNotes();
Q_INVOKABLE QString loadNote(int id);
Q_INVOKABLE QString saveNote(int id, QString text);
Q_INVOKABLE QString deleteNote(int id);
```

### QML bridge
`logos.callModule(module, method, args)` — synchronous, returns JSON string.

---

## Open Security Findings

| # | Severity | Finding | Status |
|---|----------|---------|--------|
| #10 | Low | PIN lockout counter in same DB as wrapped key. Offline attacker can reset counter between guesses. Primary defense is Argon2id cost (~0.7s/guess). Keycard (v1.0.0) eliminates this. | Open — accepted, documented |
| #8 | Low | AAD domain separation not implemented. AEAD calls don't bind note ID or schema version. | Open — future hardening |

---

## Open Questions

1. **New Logos App repo**: ✅ Resolved. `logos-co/logos-app` (now "Logos Basecamp") is the official successor. The repo is active, receiving updates, and introduced dev/portable build discrimination. `logos-app-poc` remains for reference.
2. **initLogos signature**: current code passes `LogosAPI*` but SDK headers may expect `QVariant`. Needs verification against current logos-cpp-sdk before v0.6.0 LGX work.
3. **Social backup CID discovery**: ✅ Resolved with depth. Giuliano (2026-03-17) provided detailed guidance:
   - **Permissioned groups required** — can't be permissionless or you get abuse. Trust group members by construction.
   - **Rolling updates** (new blob replaces old) looks like Status' historical message archives. No built-in "latest CID" lookup.
   - **CID delivery options**: (a) Waku messaging with MVDS/SDS for reliable delay-tolerant delivery, or (b) request/response protocol on join — broadcast ask, get CID replies. Not expensive for small groups. (c) On-chain CID placement if you don't mind gas (gasless chain available).
   - **No remote download requests** on storage nodes currently — no security model. Permissioned groups assume members are incentivized to store by construction and trust each other not to abuse.
   - **Blob size**: make blobs as large as possible for efficient network use. Batch all notes into one encrypted blob per backup, not per-note CIDs.
   - **Post-mainnet**: incentivized storage with provider protections is planned but not available yet.
   - **Decision**: permissioned trust groups via Waku messaging. Request/response CID exchange on group join. One encrypted blob per backup.
4. **Logos Storage built-in encryption**: bkomuves mentioned automatic encryption may be built-in. Not yet implemented. Worth watching before Phase 2 design.
5. **AppImage unblock**: three options identified — `QT_QML_NO_CACHEGEN=1`, `qt_deploy_qml_imports()`, or Nix bundle. Option 3 recommended. Not yet attempted.

---

## Parked Tasks

### AppImage packaging
Blocked on Qt 6.9 AOT-compiled QML type registration conflict with external plugin paths.
Upstream issue: https://github.com/logos-co/logos-app/issues/60

Unblock options (in priority order):
1. `nix bundle --bundler github:logos-co/nix-bundle-lgx` (recommended — matches logos-app-poc)
2. `qt_deploy_qml_imports()` CMake function
3. `QT_QML_NO_CACHEGEN=1` (interpreted mode, simplest)

---

## Lessons Learned

### 1. NotesPlugin is the only surface QML can see
Every method added to NotesBackend that QML needs must also be explicitly added to NotesPlugin as Q_INVOKABLE. QML callModule calls silently fail (no error, empty response) when the method doesn't exist on the plugin.

### 2. logos.callModule returns JSON, not raw text
`loadNote(id)` returns note text wrapped in JSON. QML must parse the response before assigning to `editor.text`. On error returns `{"error":"..."}` — guard against this.

### 3. Screen state must survive Qt Loader destruction
Qt's Loader destroys the previous screen when switching. Data that needs to survive a screen transition must be passed as arguments to C++ backend methods, not held in QML state. Pass backup path to `importMnemonic()` on the C++ side before any screen switch.

### 4. Restore rollback
`setInitialized()` must be called after successful restore, not before. Failed restore must roll back completely — wipe DB, return to import screen. On any failure, call `resetAndWipe()`.

### 5. Account fingerprint must be deterministic
Fingerprint derived from master key + random salt = unstable (changes per device/import). Derive from mnemonic directly with no salt: SHA-256(normalized_mnemonic) → Ed25519 seed → public key. No salt involved.

### 6. Mnemonic normalization must be shared
BIP39 validation normalizes (NFKD, lowercase, trim) but key derivation was using raw string. Same phrase typed slightly differently = different key. Single shared `normalizeMnemonic()` function, called before every crypto operation.

### 7. Logos Basecamp testing requires AppImage build
`nix build '.#app'` (local build) expects .lgx packages, not raw .so files. `cmake --install` copies raw files which only work with portable/AppImage builds.

### 8. Kill ALL Logos processes before relaunching
logos_host child processes survive parent LogosBasecamp being killed. They hold stale .so files and block new module loads: `pkill -9 -f "logos_host.elf"; pkill -9 -f "LogosBasecamp.elf"`.

### 9. QML sandbox restrictions in ui_qml plugins
- No access to Logos.Theme or Logos.Controls imports
- No native file dialogs (FileDialog blocked)
- No file I/O from QML
Workaround: hardcode hex palette values, file I/O via C++ plugin, fixed well-known paths.

### 10. plugin_metadata.json must be fully populated
Empty `{}` metadata means the shell never registers the plugin. Must match manifest.json content with correct IID.

### 11. Cipher regression from XChaCha20 fallback
Adding a "simple" AES fallback opened cipher persistence, migration, and portability bugs more complex than the problem they solved. Decision: fail-fast on AES-NI unavailability. Net -65 lines. Prefer fail-fast over complex fallback logic for edge cases affecting <0.1% of hardware.

### 12. Python brace counting is unreliable for QML validation
QML uses `{}` in JavaScript blocks differently from QML object declarations. Python counting gives wrong results. Always use `qmllint` as the authoritative QML syntax checker.

### 13. CTest must be run from build/, not repo root
Running `ctest` from repo root reports `No tests were found!!!`. CTest registers 4 tests: `test_multi_note`, `test_security`, `test_backup`, `test_account`. These are QtTest binaries with multiple internal cases — CTest will not report the per-case count.

### 14. QStandardPaths::setTestModeEnabled for backend tests
Calling `QStandardPaths::setTestModeEnabled(true)` redirects `AppDataLocation` to a test-specific path. This allows instantiating `NotesBackend` directly in tests without touching the real DB. Combined with `wipeTestData()` between tests for clean state. Established in test_backup.cpp, reused in test_account.cpp.

### 15. SQLite connections survive file permission changes
Attempted to force per-note restore failures by making the DB read-only mid-import. SQLite's cached connection continued writing. Forcing write failures in import accounting requires mock injection, not filesystem tricks.

### 16. Screen name is "note" not "notes"
`NotesBackend::importMnemonic()` and `unlockWithPin()` call `setScreen("note")`. Tests must compare against `"note"`, not `"notes"`.

### 17. nix-bundle-lgx reads metadata.json from drv.src, not lib/ output
The bundler reads `metadata.json` from `drv.src + "/metadata.json"` at Nix eval time, not from the built `lib/` directory. For a mono-repo with multiple packages, put core `metadata.json` at repo root and point UI package `src` to `./plugins/notes_ui/` which has its own `metadata.json`.

### 18. Follow nixpkgs from logos-cpp-sdk for Qt compatibility
The Logos ecosystem pins Qt via `nixpkgs.follows = "logos-cpp-sdk/nixpkgs"`. Our flake must follow the same chain. Mixing nixpkgs versions causes Qt ABI mismatches at runtime.

### 19. initLogos must NOT use override
`initLogos(LogosAPI*)` is called reflectively via `QMetaObject::invokeMethod`, not through virtual dispatch. Using `override` may cause it to not be found. LogosAPI pointer must be stored in the global `logosAPI` variable from `PluginInterface`, not in a class member.

### 20. logos-module-builder simplifies builds
The official `logos-module-builder` flake provides `mkLogosModule` + `module.yaml` — reduces ~300 lines of CMake+Nix to ~70 lines declarative config. Uses `logos_module()` CMake function. Worth migrating to for v1.1.0 (shared keycard-module). Scaffold with `nix flake init -t github:logos-co/logos-module-builder`.

### 21. LogosResult for structured returns
SDK provides `LogosResult` type with `success`, `getString()`, `getInt()`, `getMap()`, `getError()` — cleaner than our raw JSON string approach. Consider adopting for new methods.

### 22. logos-cpp-generator for typed inter-module calls
Auto-generates typed C++ wrappers from compiled modules. Instead of raw `invokeRemoteMethod("module", "method", args)`, get compile-time checked `logos->module.method(args)`. Important for v1.1.0 when keycard-module talks to notes.

### 23. JSON-RPC null error: check isNull, not contains
Go JSON-RPC responses include `"error": null` on success. `QJsonObject::contains("error")` returns true for null values. Must check `response.value("error").isNull()` instead.

### 24. AppImage sandbox hides system libraries from plugins
Plugins loaded by `logos_host` inside the AppImage can't find system `.so` files. `LD_LIBRARY_PATH` is set to AppImage paths only. Bundle all transitive dependencies (e.g. `libpcsclite.so.1`) in the module directory and use `$ORIGIN` RPATH.

### 25. Go signal callbacks don't cross logos_host IPC boundaries
Go goroutine-based callbacks (like `KeycardSetSignalEventCallback`) fire on Go threads. In the logos_host plugin architecture, these don't reliably reach the Qt event loop. Use active RPC polling (`keycard.GetStatus`) instead of relying on push signals.

### 26. CMake IMPORTED libraries embed full paths in NEEDED
`add_library(IMPORTED)` with `IMPORTED_LOCATION` embeds the absolute build path as the `NEEDED` entry in the linked binary. Use `link_directories()` + link by name instead, so the binary gets a relative `NEEDED` entry that resolves via RPATH.

### 27. install(CODE) must honor DESTDIR for staged builds
`install(CODE)` blocks run post-install scripts. When referencing installed file paths, prefix with `$ENV{DESTDIR}` so staged installs (`DESTDIR=/tmp/staged cmake --install`) work correctly.

### 28. Go JSON-RPC requires "params" field even for no-arg methods
`KeycardCallRPC` returns `{"result":null}` when the `"params"` field is omitted from the request JSON. Always include `"params":[{}]` even for methods with empty args (`*struct{}`). This was the root cause of ExportLoginKeys returning null in logos_host.

### 29. Go callbacks (signals) don't work in logos_host
Neither Session API signals (`KeycardSetSignalEventCallback`) nor Flow API signals (`keycard.flow-result`) fire reliably inside the logos_host process. The Go goroutine thread can't reach the plugin. Use RPC polling (`GetStatus`) instead of push signals. For key export, use synchronous RPC calls, not the Flow API.

### 30. Keycard accounts don't store wrapped keys
Mnemonic accounts wrap the master key with a PIN-derived key and store it in `wrapped_key` table. Keycard accounts derive the key from the card on every unlock — no wrapped key stored. The `key_source` meta field ("keycard" or "mnemonic") determines which unlock flow to use.

### 31. AppImage wraps processes via ld-linux — pkill must match .elf names
`pkill -9 -f logos` doesn't work because AppImage runs binaries as `/lib64/ld-linux-x86-64.so.2 /tmp/.mount_logos-XXX/usr/bin/.LogosBasecamp.elf`. Must use `pkill -9 -f "LogosBasecamp.elf"; pkill -9 -f "logos_host.elf"` to kill reliably. Two instances fighting over the same DB causes data loss.

### 32. SVG Image elements in QML sandbox need z-order for MouseArea
Loading SVG icons via `Image { source: "file.svg" }` works in the Logos App QML sandbox, but the Image can block mouse events. Always put `MouseArea { z: 10 }` to ensure clicks pass through. Also set `sourceSize: Qt.size(w, h)` for proper SVG rendering.

### 33. Logos Basecamp loads old module versions from backup directories
During development, backup directories like `notes.bak`, `notes.old`, etc. can persist in `~/.local/share/Logos/LogosBasecamp{Dev}/modules/`. The shell may load these stale versions instead of the current build from `notes/`, causing confusing failures (e.g. Keycard detection working in tests but not in the app). **Solution**: CMake install target now removes all `notes.*` directories before installing the current build. This prevents version conflicts and ensures only one module version exists at a time.

### 34. install(CODE) blocks must honor DESTDIR for staged installs
Custom `install(CODE)` blocks that manipulate filesystem paths must prefix those paths with `$ENV{DESTDIR}` to support staged/packaged installs. Example: `set(_path "\$ENV{DESTDIR}${INSTALL_DIR}/file")`. Without this, `DESTDIR=/tmp/stage cmake --install` would still operate on the live system paths instead of the staged tree. This is the same pattern required for post-install scripts like `patchelf`. Caught by Senty in #47 review.

### 35. nix-bundle-lgx platform naming: default vs portable
The default bundler (`nix bundle --bundler github:logos-co/nix-bundle-lgx .#lib`) generates `linux-amd64-dev` variant names. The Logos App Package Manager expects `linux-amd64` without the `-dev` suffix. Use the portable bundler (`#portable`) for correct platform recognition: `nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib`. This bundles all dependencies (including system libraries) for true portability but see lesson #36 for caveats.

### 36. Bundled libpcsclite breaks pcscd socket connection
The portable bundler includes all transitive dependencies, including `libpcsclite.so.1` for smart card support. However, the bundled version cannot connect to the system `pcscd` daemon socket (looks for socket in wrong location). Smart card detection fails. **Solution**: Use the `nix run .#package-lgx` command, which bundles with the portable bundler then automatically removes libpcsclite, producing a shippable LGX that uses the system libpcsclite for proper pcscd connectivity. This applies to any library that interacts with system services via local sockets.

### 37. Logos App renamed to Logos Basecamp (March 2026)
The `logos-co/logos-app` repository was renamed to "Logos Basecamp" (commit 17ef99c). Module paths changed:
- **Portable builds** (AppImage/LGX): `~/.local/share/Logos/LogosBasecamp/{modules,plugins}/`
- **Dev builds** (cmake install): `~/.local/share/Logos/LogosBasecampDev/{modules,plugins}/`
The app now discriminates between dev and portable package variants at build time (`LOGOS_PORTABLE_BUILD` flag). Binary names changed: `LogosApp.elf` → `LogosBasecamp.elf`, `logos-app.AppImage` → `logos-basecamp.AppImage`. Update all install paths and documentation.

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

### Network ports
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

### Key findings (tested 2026-03-12)
- Upload → CID ✅, fetch while online ✅
- Blobs stored locally only — no auto-replication to peers
- Delete = gone permanently if no other node has fetched it
- Single device limitation: full sync requires minimum 2 nodes
- Works on cellular with `nat: "none"` in config
- **Storage team confirmed**: web-of-trust peer storage is intended usage

### Integration path for Phase 2
- Notes module calls storage_module via `LogosAPIClient::invokeRemoteMethod()`
- Upload: `uploadInit(filepath, chunkSize)` → `uploadChunk()` → `uploadFinalize()` → CID
- Download: `downloadToUrl(cid, destUrl, local, chunkSize)` → file at destination
- All operations async via `eventResponse` signal

---

## Import Path Vision (v1.0.0+)

Three ways to derive the same 256-bit AES-256-GCM master key:

```
Import Screen:
  ┌─ 1. Enter Recovery Phrase ──→ Argon2id → master key (current, always available)
  ├─ 2. Connect Keycard ────────→ card derives m/43'/60'/1581' → master key (#33)
  └─ 3. Connect Logos Wallet ───→ accounts_module derives key → master key (#32)
```

Same DB, same encryption, same notes. User picks their preferred key management.
Switching between methods: re-encrypt via backup/restore flow.
Keycard (#33) and wallet (#32) are independent features — neither blocks the other.

### Keycard integration (v1.0.0)

- Hardware: Status Keycard (ISO 7816) + USB PC/SC reader
- Library: `status-keycard-go` compiled as `libkeycard.so`, thin C++ wrapper (`KeycardBridge`)
- Key derivation: BIP44 path `m/43'/60'/1581'` (EIP-1581 encryption root)
- Note: Keycard uses secp256k1, current fingerprint uses Ed25519 — need domain separation
- Phase 1: link into notes_plugin directly. Phase 2: extract shared keycard-module for ecosystem.
- Reference: `~/status-desktop/vendor/status-keycard-go/` and `vendor/status-keycard-qt/`
- Build: `scripts/build-libkeycard.sh` compiles Go → `lib/keycard/libkeycard.so`
- C API: `KeycardInitializeRPC()`, `KeycardCallRPC()`, `KeycardSetSignalEventCallback()`, `Free()`
- JSON-RPC methods: `keycard.Start`, `keycard.Stop`, `keycard.GetStatus`, `keycard.Authorize`, `keycard.ExportRecoverKeys`

#### Issue #44: Build libkeycard from source (2026-03-19)

**Problem**: Pre-built `libkeycard.so` binary committed to repo (14MB, commit 76c8804)

**Solution**: Build from source in Nix flake using `pkgs.buildGoModule`:
```nix
libkeycard = pkgs.buildGoModule {
  src = pkgs.fetchFromGitHub {
    owner = "status-im";
    repo = "status-keycard-go";
    rev = "76c880480c62dbf0ee67ee342f87ab80a928ed73";
  };
  buildPhase = ''
    cd shared
    export CGO_ENABLED=1
    go build -buildmode=c-shared -o libkeycard.so .
  '';
};
```

**Backup strategy**: Keep committed binary at `lib/keycard/libkeycard.so` as fallback for several months until Nix builds proven stable.

**LGX Packaging findings**:
1. **Platform mismatch**: Default bundler generates `linux-amd64-dev`, Logos App expects `linux-amd64`
   - Solution: Use `nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib`
2. **libpcsclite issue**: Portable bundler includes `libpcsclite.so.1` (libkeycard dependency), but bundled version cannot connect to system pcscd daemon socket
   - Root cause: Bundled library looks for pcscd socket in wrong location
   - Workaround: Remove `libpcsclite.so.1` from `modules/notes/` after LGX installation to force system version
   - Documented in `flake.nix` with inline comment

**Testing**: ✅ Full LGX workflow validated with both core and UI modules, smart card detection working after libpcsclite workaround.

#### Sub-issue tracker
| # | Title | Branch | Status |
|---|-------|--------|--------|
| #34 | Reader detection + card state UI | merged to master | ✅ Complete — hardware verified, Codex LGTM |
| #35 | PIN authorization + key export | merged to master | ✅ Complete — Codex LGTM, hardware verified |
| #36 | Wire key into NotesBackend encryption | merged to master | ✅ Complete — same merge as #35 |
| #37 | Keycard ↔ mnemonic migration path | — | Postponed — not needed for v1.0.0 |
| #44 | Build libkeycard from source + LGX packaging | feature/shared-keycard-module | ✅ Complete — ready for merge after review |

### Wallet integration (v0.7.0+)

- `accounts_module` wraps `go-wallet-sdk` — provides `createRandomMnemonic()`, HD keystore, PIN management
- Lightweight option: "Generate with Wallet" button on import screen (~10 lines QML)
- Full option: wallet holds the key, notes requests it via LogosAPI

---

## Phase 3 — Trust Network (proposal)

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

## PluginInterface — Base Class Definition

From `/nix/store/092zxk8qbm9zxqigq1z0a5l901a068cz-logos-liblogos-headers-0.1.0/include/interface.h`:

```cpp
class PluginInterface {
public:
    virtual ~PluginInterface() {}
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    LogosAPI* logosAPI = nullptr;
};
#define PluginInterface_iid "com.example.PluginInterface"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)
```

Current IID in use: `"org.logos.NotesModuleInterface"` (verified in plugin_metadata.json).

**Open question**: `initLogos` signature — old code used `LogosAPI*`, SDK headers may expect
`QVariant`. Needs verification against current logos-cpp-sdk before v0.6.0 LGX work.

---

## Logos Core C API (how the shell loads modules)

```c
logos_core_set_plugins_dir(dir);        // tell core where .so files live
logos_core_start();                     // start core process
logos_core_load_plugin("notes");        // load our plugin
logos_core_call_plugin_method_async(    // call a method with JSON params
    "notes", "initialize", "[]", callback, userData);
logos_core_register_event_listener(    // subscribe to eventResponse signal
    "notes", "eventResponse", callback, userData);
```

---

## LGX Package Format

LGX files are `tar.gz` archives containing platform-specific module variants. Built with `nix-bundle-lgx`.

### manifest.json schema (full)

```json
{
  "name": "notes",
  "version": "1.0.0",
  "type": "core",
  "category": "notes",
  "author": "xAlisher",
  "description": "Encrypted local-first notes",
  "icon": "",
  "main": {
    "linux-amd64": "notes_plugin.so",
    "darwin-arm64": "notes_plugin.dylib"
  },
  "manifestVersion": "0.1.0",
  "dependencies": []
}
```

### Build command

```bash
# IMPORTANT: Use #portable bundler for correct platform (linux-amd64 not linux-amd64-dev)
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#ui

# Default bundler generates linux-amd64-dev which Package Manager rejects
# nix bundle --bundler github:logos-co/nix-bundle-lgx .#lib  # WRONG
```

### What goes in the LGX

| Module | Type | Contents |
|--------|------|----------|
| `notes.lgx` | core | `manifest.json` + `variants/linux-amd64/notes_plugin.so` + bundled deps (libkeycard, libsodium) |
| `notes_ui.lgx` | ui_qml | `manifest.json` + `variants/linux-amd64/Main.qml` + `metadata.json` |

### Post-install workaround

**Smart card detection fix**: After installing `notes.lgx`, remove the bundled `libpcsclite.so.1`:
```bash
rm ~/.local/share/Logos/LogosBasecamp/modules/notes/libpcsclite.so.1
```
This forces usage of system libpcsclite which properly connects to the pcscd daemon socket.

### Current state

✅ **LGX packaging complete** (issue #44, 2026-03-19)
- Core and UI modules install via Package Manager
- Portable bundler generates correct `linux-amd64` platform
- libpcsclite workaround documented in flake.nix
- Full workflow tested with smart card detection

`cmake --install` still works for AppImage-based testing.

---

## Installed Modules in Running Logos Basecamp

Verified by inspecting `~/.local/share/Logos/LogosBasecamp/` after Downloads AppImage installs them.

| Module | Type | Description |
|--------|------|-------------|
| `chat` | core | Classic Waku relay chat |
| `chat-mix` | core | Chat via mixnet (AnonComms) |
| `chatsdk_module` | core | LogosChat SDK (nim-chat-poc C FFI wrapper) |
| `storage_module` | core | Logos Storage node (Codex/libp2p) |
| `chat_ui` | ui_qml | Chat UI plugin |
| `storage_ui` | ui | Storage UI (compiled .so with embedded QML) |
| `notes` | core | Our module |
| `notes_ui` | ui_qml | Our UI plugin |

### ChatSDK Q_INVOKABLE methods (reference for IPC patterns)
```
connect(url)
disconnect()
sendMessage(roomId, text)
joinRoom(roomId)
leaveRoom(roomId)
getMessages(roomId)
getRooms()
```

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
