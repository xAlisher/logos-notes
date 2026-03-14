# Immutable Notes — Logos Testnet Dapp

> Read this file at the start of every session before touching any code.

## What We're Building

Encrypted, local-first Markdown notes app for the Logos ecosystem.
Approved idea: https://github.com/logos-co/ideas/issues/13

The app lives inside **Logos App (Basecamp)** — the Qt6 module host shell.
Pattern to follow: logos-chat-ui (C++/QML, core+ui module pair).

---

## Repositories (all on GitHub under logos-co org)

| Repo | Purpose |
|------|---------|
| https://github.com/logos-co/logos-app-poc | Shell host — loads our module |
| https://github.com/logos-co/logos-chat-ui | Primary reference dapp (C++/QML) |
| https://github.com/logos-co/logos-chat-module | Chat core module reference |
| https://github.com/logos-co/logos-template-module | Scaffold starting point |
| https://github.com/logos-co/logos-cpp-sdk | C++ SDK |
| https://github.com/logos-co/logos-docs | Docs |
| https://github.com/logos-storage/logos-storage-nim | Storage (no public docs yet) |

Local clones already present:
- `~/logos-app/` — logos-app-poc (built, AppImage runs)
- `~/status-desktop/` — mature QML/Nim reference app (same stack)

---

## Tech Stack

- **Language**: C++17
- **UI**: Qt 6.9.3 QML — at `~/Qt/6.9.3/gcc_64/`
  - QtBase, QtQML, QtQuick, Qt Remote Objects (IPC between modules)
  - Qt SQL (SQLite)
- **Crypto**: libsodium 1.0.18
  - AES-256-GCM for note encryption
  - Argon2id for key derivation from BIP39 mnemonic
- **Storage**: SQLite via Qt SQL module
- **Build**: CMake 3.28 + Nix flake (matches logos-app-poc)
- **Design tool**: Pencil.dev (same as logos-chat-ui author)
- **OS**: Ubuntu 24.04

### System packages installed
```
qt6-base-dev, qt6-declarative-dev, cmake, ninja-build,
build-essential, pkgconf, libsodium-dev, libsqlite3-dev, nix
```

---

## Phase 0 Scope — ONLY THIS, nothing more

**Objective**: Working local-first encrypted note app, standalone Qt/QML desktop app on Ubuntu.

### 3 screens

1. **Import screen** — Enter BIP39 recovery phrase + set PIN (confirm PIN)
2. **Unlock screen** — Enter PIN to decrypt session key (return visits)
3. **Note screen** — Single plain text editor, auto-saves, always the same note

### Decisions locked in

- **Import only** — no mnemonic generation (testers already have phrases)
- **One note** — no list, no titles, no folders
- **Plain text** — no Markdown preview
- **AES-256-GCM from day one** — no placeholder crypto
- **PIN minimum length** — validate + confirm entry
- **No purge/reset flow** needed for Phase 0

### Explicitly OUT of scope for Phase 0

Markdown preview, note list, mnemonic generation, Keycard, Logos Messaging,
Logos Storage, purge flow, settings, dark/light mode, export/backup,
auto-save indicator, note titles, multi-device sync.

---

## Encryption Architecture

```
BIP39 mnemonic
    → Argon2id (libsodium) with fixed salt derived from mnemonic
    → 256-bit master key (never stored)

PIN
    → protects in-memory session key only
    → on lock: key wiped from memory

Note content
    → AES-256-GCM encrypt with master key
    → store ciphertext + nonce in SQLite
    → plaintext never touches disk
```

### Phase migration path (no data migration needed)
- **Phase 1**: swap Argon2 software key derivation → Keycard hardware key
  (same PIN concept maps directly, same SQLite schema)
- **Phase 2**: add Logos Messaging sync on top of existing encrypted store

---

## Module Package Structure

The Package Manager (visible in Logos App) expects a `core` + `ui` pair:

```
logos-notes-core    type: core    C++ backend (crypto, SQLite, key mgmt)
logos-notes-ui      type: ui      QML frontend (3 screens)
```

Every installed module shows in the sidebar via `backend.launcherApps`.
`SidebarAppDelegate` shows 4-char text fallback if no icon — use short name `note`
or provide a proper icon.

---

## Plugin Contract — How Core Modules Integrate with Logos App

> Verified by reading logos-chat-module source (`chat_interface.h`, `chat_plugin.h`) and logos-cpp-sdk headers.

### The mandatory sequence

```
Shell → initLogos(LogosAPI*)   // inject SDK — called first, before anything else
Shell → initialize()            // start the module (connect, open DB, etc.)
Module → eventResponse(name, data)  // all async events go back through this ONE signal
```

logos-notes-core must implement exactly this same contract.

### PluginInterface (from logos-liblogos `interface.h`)

```cpp
#include "logos_api.h"

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

Headers live in the Nix store at:
- `/nix/store/092zxk8qbm9zxqigq1z0a5l901a068cz-logos-liblogos-headers-0.1.0/include/interface.h`
- `/nix/store/047dmhc4gi7yib02i1fbwidxpksqvcc2-logos-cpp-sdk/include/cpp/logos_api.h`

### What logos-notes-core plugin class must look like

```cpp
class NotesPlugin : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid FILE "metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    QString name() const override { return "notes"; }
    QString version() const override { return "1.0.0"; }

    Q_INVOKABLE void initLogos(LogosAPI* api);   // shell calls this first
    Q_INVOKABLE bool initialize();                // shell calls this second

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);
    // ↑ all async results/events go through this single signal
};
```

### LogosAPI surface (logos-cpp-sdk)

```cpp
// LogosAPI is the SDK entry point injected by the shell
class LogosAPI : public QObject {
    LogosAPIProvider* getProvider() const;                     // register objects for remote access
    LogosAPIClient*   getClient(const QString& targetModule);  // call another module
    TokenManager*     getTokenManager() const;                 // auth tokens
};

// To call another core module:
auto* client = logosAPI->getClient("package_manager");
QVariant result = client->invokeRemoteMethod("SomeObject", "someMethod", args);

// To listen for events from another module:
client->onEvent(originObj, this, "eventName", [](auto name, auto data) { ... });

// To expose this module's objects to other modules:
logosAPI->getProvider()->registerObject("NotesBackend", backendObj);
```

### metadata.json (core module)

```json
{
  "name": "notes",
  "version": "1.0.0",
  "type": "core",
  "category": "notes",
  "main": "notes_plugin",
  "dependencies": []
}
```

`type: "core"` means it runs as a background plugin process. The companion `type: "ui"` entry
is the QML plugin that creates the visible widget.

### Logos core C API (how the shell loads modules)

```c
logos_core_set_plugins_dir(dir);       // tell core where .so files live
logos_core_start();                    // start core process
logos_core_load_plugin("notes");       // load our plugin
logos_core_call_plugin_method_async(   // call a method with JSON params
    "notes", "initialize", "[]", callback, userData);
logos_core_register_event_listener(   // subscribe to our eventResponse signal
    "notes", "eventResponse", callback, userData);
```

---

## Logos App Shell — Key QML Patterns

```qml
// Sidebar reads these from backend:
property var launcherApps: backend.launcherApps      // our module appears here
property var sections: backend.sections              // workspace/view sections

// App registration fields:
modelData.name        // displayed as label
modelData.iconPath    // icon, fallback = first 4 chars of name
modelData.isLoaded    // loaded=true → backgroundTertiary, false → overlayDark

// Active app indicator:
checked: modelData.name === backend.currentVisibleApp

// Theme imports:
import Logos.Theme
import Logos.Controls
Theme.palette.backgroundSecondary   // sidebar bg
Theme.palette.accentOrange          // active left border
Theme.palette.textSecondary         // text color
Theme.spacing.large / .small / .tiny
```

---

## Status Desktop QML Patterns (reference at ~/status-desktop/)

Seed phrase import flow lives in:
`~/status-desktop/ui/imports/shared/popups/addaccount/`

Key pattern — QML calls store, store calls module:
```qml
// QML
root.store.validSeedPhrase(seedPhrase)   // validate
root.store.changeSeedPhrase(seedPhrase)  // set

// Store delegates to C++ module
function validSeedPhrase(s) { return root.addAccountModule.validSeedPhrase(s) }
function changeSeedPhrase(s) { root.addAccountModule.changeSeedPhrase(s) }
```

Use `StatusQ` components as design reference but implement with `Logos.Controls`
(or plain QtQuick.Controls) since we don't have StatusQ.

---

## Project File Structure (to be created)

```
logos-notes/
├── CLAUDE.md                  ← this file
├── CMakeLists.txt
├── flake.nix
├── metadata.json              ← declares module to Logos App
├── src/
│   ├── core/                  ← C++ backend
│   │   ├── NotesBackend.h/cpp
│   │   ├── CryptoManager.h/cpp   (libsodium wrapper)
│   │   ├── DatabaseManager.h/cpp (SQLite)
│   │   └── KeyManager.h/cpp      (BIP39 + Argon2 → session key)
│   └── ui/                    ← QML frontend
│       ├── main.qml
│       ├── screens/
│       │   ├── ImportScreen.qml
│       │   ├── UnlockScreen.qml
│       │   └── NoteScreen.qml
│       └── components/
│           └── PinInput.qml
├── assets/
│   └── icon.png
└── tests/
    └── crypto_test.cpp
```

---

## NotesBackend Q_INVOKABLE API

### Phase 0 (single note, id=1)

| Method | Signature | Returns |
|--------|-----------|---------|
| `importMnemonic` | `(QString mnemonic, QString pin, QString pinConfirm)` | void — navigates to note screen |
| `unlockWithPin` | `(QString pin)` | void — navigates to note screen |
| `saveNote` | `(QString plaintext)` | void — encrypts + saves as id=1 |
| `loadNote` | `()` | `QString` — decrypted note text for id=1 |
| `lock` | `()` | void — wipes key, navigates to unlock |
| `resetAndWipe` | `()` | void — deletes DB, navigates to import |

### Phase 1 (multi-note CRUD) — 9/9 unit tests pass

| Method | Signature | Returns |
|--------|-----------|---------|
| `createNote` | `()` | `QString` JSON `{"id": N}` |
| `loadNotes` | `()` | `QString` JSON array `[{"id", "title", "updatedAt"}, ...]` sorted by most recent |
| `loadNote` | `(int id)` | `QString` decrypted plaintext |
| `saveNote` | `(int id, QString plaintext)` | `QString` `"ok"` — auto-extracts title from first line, sets updated_at |
| `deleteNote` | `(int id)` | `QString` `"ok"` |

Schema migration adds `title TEXT` and `updated_at INTEGER` columns non-destructively.
Run tests: `cmake --build build --target test_multi_note && ./build/test_multi_note`

---

## Build Commands

```bash
# Configure (use Qt 6.9.3, not system Qt)
cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=~/Qt/6.9.3/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run standalone (Phase 0) — must set QML_IMPORT_PATH for Logos.Theme / Logos.Controls
QML_IMPORT_PATH=/nix/store/w9ra12n0yabd275v33m8x7lqnnrcgb9f-logos-design-system-1.0.0/lib \
  ./build/logos-notes

# Run inside Logos App (later)
./result/logos-app.AppImage  # loads modules from known paths
```

---

## Current Status

- [x] Environment set up (Ubuntu 24.04, Qt 6.9.3, libsodium, CMake, Nix)
- [x] logos-app-poc cloned and AppImage running at ~/logos-app/
- [x] status-desktop cloned at ~/status-desktop/ (QML reference)
- [x] Logos App shell QML structure understood (SidebarPanel, DashboardView)
- [x] Package Manager module pattern understood (core+ui pairs)
- [x] Phase 0 scope locked and documented
- [x] Encryption architecture decided
- [x] Create GitHub repo — pushed to github.com/xAlisher/logos-notes, SSH auth configured
- [x] Scaffold CMakeLists.txt + flake.nix from logos-template-module
- [x] Implement CryptoManager (libsodium AES-256-GCM + Argon2)
- [x] Implement DatabaseManager (SQLite schema)
- [x] Implement KeyManager (BIP39 validation + key derivation)
- [x] Build 3 QML screens
- [x] Wire C++ backend to QML via Q_PROPERTY / signals
- [x] Test end-to-end: import → unlock → write note → kill app → unlock → note still there
- [x] Phase 1: Multi-note backend (DatabaseManager + NotesBackend CRUD, 9/9 tests pass)
- [ ] Register as Logos App module (metadata.json + plugin interface)
- [ ] Update https://github.com/logos-co/ideas/issues/13 with Phase 0 definition

### Distribution targets (parallel, not sequential)

| Target | Version | Description | Status |
|--------|---------|-------------|--------|
| **AppImage** | v0.4.0 | Standalone desktop app, runs without Logos App | Planned |
| **LGX package** | v0.5.0 | Installs inside Logos App via Package Manager | Planned |

---

## Installed Modules in Running Logos App

> Verified by running `~/.local/share/Logos/LogosApp/` after the Downloads AppImage installs them.
> Module `.so` files are at `~/.local/share/Logos/LogosApp/modules/<name>/` and
> UI plugins at `~/.local/share/Logos/LogosApp/plugins/<name>/`.

### Full module inventory

| Module | Type | Dependencies | Description |
|--------|------|-------------|-------------|
| `chat` | core | `waku_module` | Classic Waku relay chat |
| `chat-mix` | core | `waku_module` | Chat via mixnet (AnonComms) |
| `chatsdk_module` | core | _(none)_ | LogosChat SDK (nim-chat-poc C FFI wrapper) |
| `storage_module` | core | _(none)_ | Logos Storage (logos-storage-nim C FFI wrapper) |
| `delivery_module` | core | _(none)_ | High-level message-delivery API |
| `waku_module` | core | _(none)_ | Waku P2P transport |
| `accounts_module` | core | _(none)_ | Accounts via go-wallet-sdk |
| `package_manager` | core | _(none)_ | Module installer |
| `capability_module` | core | _(none)_ | Capability discovery |
| `chat_ui` | ui | `chat` | Chat UI (classic) |
| `chat_ui_mix` | ui | `chat` | Chat UI (mixnet) |
| `chatsdk_ui` | ui | `chatsdk_module` | Private messaging UI |
| `storage_ui` | ui | `storage_module` | Storage UI |
| `accounts_ui` | ui | `accounts_module` | Accounts UI |

### `manifest.json` schema (real format — use this for logos-notes)

```json
{
  "author": "...",
  "category": "notes",
  "dependencies": [],
  "description": "...",
  "icon": "icon.png",
  "main": {
    "linux-amd64": "notes_plugin.so",
    "darwin-arm64": "notes_plugin.dylib"
  },
  "manifestVersion": "0.1.0",
  "name": "notes",
  "type": "core",
  "version": "1.0.0"
}
```

Note: the real field is `"main": { "linux-amd64": "..." }` (platform map), **not** `"main": "notes_plugin"`.

### `ChatSDKModulePlugin` Q_INVOKABLE methods (extracted from binary)

These are what the chatsdk_ui calls via `LogosAPIClient::invokeRemoteMethod`:

| Method | Signature |
|--------|-----------|
| `initChat` | `initChat(QString config)` |
| `stopChat` | `stopChat()` |
| `destroyChat` | `destroyChat()` |
| `getId` | `getId()` |
| `getIdentity` | `getIdentity()` |
| `createIntroBundle` | `createIntroBundle()` |
| `listConversations` | `listConversations()` |
| `getConversation` | `getConversation(QString convoId)` |
| `newPrivateConversation` | `newPrivateConversation(QString introBundle, QString content)` |
| `sendMessage` | `sendMessage(QString convoId, QString content)` |

Signal: `eventResponse(QString eventName, QVariantList data)` — all async results come back here.
Internal callbacks: `init_callback`, `start_callback`, `event_callback`, etc. (bridge to C FFI).

### `StorageModulePlugin` Q_INVOKABLE methods (extracted from binary)

| Method | Signature |
|--------|-----------|
| `initLogos` | `initLogos(LogosAPI*)` — shell injection |
| `init` | `init(QString configJson)` |
| `start` | `start()` |
| `stop` | `stop()` |
| `destroy` | `destroy()` |
| `version` | `version()` |
| `spr` | `spr()` — Signed Peer Record |
| `peerId` | `peerId()` |
| `debug` | `debug()` |
| `dataDir` | `dataDir()` |
| `connect` | `connect(QString peerId, QList<QString> addrs)` |
| `updateLogLevel` | `updateLogLevel(QString level)` |
| `uploadUrl` | `uploadUrl(QUrl url, int chunkSize)` |
| `uploadInit` | `uploadInit(QString filepath, int chunkSize)` |
| `uploadChunk` | `uploadChunk(QString sessionId, QByteArray data)` |
| `uploadFinalize` | `uploadFinalize(QString sessionId)` |
| `uploadCancel` | `uploadCancel(QString sessionId)` |
| `importFiles` | `importFiles(QString path)` |
| `downloadToUrl` | `downloadToUrl(QString cid, QUrl dest, bool local, int chunkSize)` |
| `downloadChunks` | `downloadChunks(QString cid, bool local, int chunkSize, QString filepath)` |
| `downloadManifest` | `downloadManifest(QString cid)` |
| `downloadCancel` | `downloadCancel(QString cid)` |
| `manifests` | `manifests()` — list local CIDs |
| `space` | `space()` |
| `exists` | `exists(QString cid)` |
| `fetch` | `fetch(QString cid)` — pull from network |
| `remove` | `remove(QString cid)` |

Signal: `eventResponse(QString eventName, QVariantList data)`
Also: `storageResponse(StorageSignal, int, QString)` — internal signal bridging C FFI callbacks.

---

## Key Contacts / Links

- Ideas issue: https://github.com/logos-co/ideas/issues/13
- Logos App PoC: https://github.com/logos-co/logos-app-poc
- Chat UI reference: https://github.com/logos-co/logos-chat-ui
- Waku docs (messaging, Phase 2): https://docs.waku.org
- Logos Storage (Phase 2): https://github.com/logos-storage/logos-storage-nim
- nim-chat-poc / LogosChat (Phase 2 messaging): https://github.com/logos-messaging/nim-chat-poc
  — C FFI: `liblogoschat.so` + `library/liblogoschat.h`, async callback pattern identical to logos-storage-nim

---

## Open Questions

- **`initLogos` signature discrepancy** — `chat_interface.h` declares `initLogos(LogosAPI*)` but
  the `storage_module_plugin.so` binary shows `initLogos(QVariant)`. Must verify against actual
  running modules (inspect `chatsdk_module_plugin.so` and `chat_plugin.so` symbols) before
  implementing `PluginInterface` in logos-notes-core. Getting this wrong silently breaks loading.

---

## Parked Tasks

### AppImage packaging
Status: blocked on QML type resolution at runtime.
Root cause: Qt 6.9 AOT-compiled QML bakes type registrations
via QRC `prefer` directives that override external plugin paths.

Three unblock options:
1. Rebuild with `QT_QML_NO_CACHEGEN=1` (interpreted mode)
2. Use `qt_deploy_qml_imports()` CMake function
3. Nix-based approach matching logos-app-poc (`nix-bundle-appimage`)

Option 3 is cleanest — matches how the Logos App itself packages.
Revisit after Settings screen and alpha prep are done.

---

## Storage Module Research

> Researched 2026-03-12 by reverse-engineering installed binaries and GitHub sources.

### Architecture

The storage module is a **Codex node** (decentralised filesharing) wrapped for Logos App:

```
libstorage.so          — Nim library (logos-storage-nim), ~14 MB
                         Built from codexdht + nim-leveldb + libp2p
storage_module_plugin.so — Qt C++ wrapper (PluginInterface, Q_INVOKABLE methods)
storage_ui.so          — QML UI plugin (StorageBackend class + embedded QML)
```

Source repos:
- Nim library: `https://github.com/logos-storage/logos-storage-nim`
- Vendor deps inside: `logos-storage-nim-dht/codexdht` (DiscoveryV5), `nim-leveldbstatic`

### C FFI — `libstorage.so`

```c
typedef void (*StorageCallback)(int callerRet, const char *msg, size_t len, void *userData);

void *storage_new(const char *configJson, StorageCallback callback, void *userData);
int   storage_start(void *ctx, StorageCallback callback, void *userData);
int   storage_stop(void *ctx, StorageCallback callback, void *userData);
int   storage_close(void *ctx, StorageCallback callback, void *userData);
int   storage_destroy(void *ctx);

// Return codes: RET_OK=0, RET_ERR=1, RET_MISSING_CALLBACK=2, RET_PROGRESS=3
```

All operations are async — dispatched to a background thread, results arrive via callback.
`RET_PROGRESS` can fire multiple times before final `RET_OK`/`RET_ERR`.

### Config JSON keys (passed to `storage_new` / `StorageModulePlugin::init`)

Extracted from `libstorage.so` symbol table. Priority order: CLI > env > config file.

| Key | JSON alias | Default | Purpose |
|-----|-----------|---------|---------|
| `listen-port` | `listenPort` | **8500** | libp2p TCP listen port |
| `listen-ip` | `listenIp` | `0.0.0.0` | libp2p bind IP |
| `disc-port` | `discoveryPort` | **8090** | DiscoveryV5 UDP port |
| `api-port` | `apiPort` | **8080** | REST API port (`/api/storage/v1/...`) |
| `api-bindaddr` | `apiBindAddress` | `127.0.0.1` | REST API bind address |
| `api-cors-origin` | `apiCorsAllowedOrigin` | — | CORS header for REST API |
| `data-dir` | `dataDir` | — | Node data directory (LevelDB + blocks) |
| `bootstrap-node` | `bootstrapNodes` | — | Multiaddr(s) of bootstrap peers |
| `max-peers` | `maxPeers` | — | Maximum connected peers |
| `cache-size` | `cacheSize` | — | Block cache size |
| `block-ttl` | `blockTtl` | — | Block time-to-live |
| `block-mi` | `blockMaintenanceInterval` | — | Block maintenance interval |
| `block-mn` | `blockMaintenanceNumberOfBlocks` | — | Blocks per maintenance cycle |
| `block-retries` | `blockRetries` | — | Block fetch retry count |
| `storage-quota` | `storageQuota` | — | Max bytes for storage |
| `log-level` | `logLevel` | — | Logging level |
| `log-file` | `logFile` | — | Log output file |
| `log-format` | `logFormat` | — | Log format |
| `metrics` | `metricsEnabled` | — | Enable metrics |
| `metrics-port` | `metricsPort` | — | Metrics endpoint port |
| `metrics-address` | `metricsAddress` | — | Metrics bind address |
| `net-privkey` | `netPrivKeyFile` | — | libp2p private key file |
| `repo-kind` | `repoKind` | — | Repository backend type |
| `nat` | — | — | NAT traversal mode (`none`, `upnp`, `pmp`, `any`) |

### Network ports summary

| Port | Protocol | Service |
|------|----------|---------|
| 8500 | TCP | libp2p transport (peer connections, block exchange) |
| 8090 | UDP | DiscoveryV5 (peer discovery, DHT) |
| 8080 | TCP | REST API (`http://localhost:8080/api/storage/v1/...`) |

### REST API endpoints (at `api-port`)

```
GET    /api/storage/v1/data                  — list local CIDs
POST   /api/storage/v1/data                  — upload file (streaming)
GET    /api/storage/v1/data/{cid}            — download from local
DELETE /api/storage/v1/data/{cid}            — delete
POST   /api/storage/v1/data/{cid}/network    — fetch from network (async)
GET    /api/storage/v1/data/{cid}/exists     — check existence
GET    /api/storage/v1/space                 — storage quota/usage
GET    /api/storage/v1/connect/{peerId}      — connect to peer
GET    /api/storage/v1/spr                   — Signed Peer Record
GET    /api/storage/v1/peerid               — node's Peer ID
GET    /api/storage/v1/debug/info            — node debug info
POST   /api/storage/v1/debug/chronicles/loglevel — set log level
```

Full spec: https://api.codex.storage

### How `storage_ui` configures the node at runtime

1. `StorageBackend::defaultConfig()` generates a default JSON config (listen port 8500, disc port 8090)
2. `StorageBackend::loadUserConfig()` reads user's saved config from `<dataDir>/config.json`
   — falls back to default if file missing or invalid
3. QML exposes a `JsonEditor` component that lets user edit raw JSON config
4. `StorageBackend::saveUserConfig(json)` persists modified config
5. `StorageBackend::init(configJson)` → calls `StorageModule::init(configJson)`
   → which calls C FFI `storage_new(configJson, callback, userData)`
6. `StorageBackend::start()` → `storage_start(ctx, ...)`
7. NAT config: `enableUpnpConfig()` or `enableNatExtConfig(tcpPort)` for manual port forwarding
8. `checkNodeIsUp()` calls `storage_debug()`, checks for peers, then calls
   `https://portchecker.io/api/` to verify external reachability

### How it connects to peers (Waku / P2P)

- Storage does **not** use Waku directly — it uses **libp2p + DiscoveryV5 (codexdht)**
- Peer discovery: UDP DiscoveryV5 on `disc-port` (8090) — same protocol family as Ethereum
- Block exchange: TCP libp2p on `listen-port` (8500) — custom bitswap-like protocol
- Bootstrap: `bootstrap-node` config key accepts multiaddr(s) of known peers
- `connect(peerId, addrs)` method for manual peer connection
- `announceAddresses` — external addresses advertised to other peers
- NAT traversal supported via UPnP or NAT-PMP

### Data storage

- **LevelDB** for metadata (via `nim-leveldbstatic`)
- Block data stored in `data-dir` directory
- No persisted config file by default — `storage_ui` manages `config.json` in the data dir
- `storage_module_plugin.so` itself has no companion config files

### Phase 0 Testing Results (2026-03-12)

Tested on cellular connection (restrictive NAT).

**NAT & connectivity:**
- Storage node works on cellular with `nat: "none"` in Advanced config
- UPnP and Port Forwarding both fail on cellular/restrictive NAT — the onboarding
  wizard blocks Continue on reachability failure (UX bug worth reporting to Logos team)
- **Workaround**: use Advanced config, set `nat: "none"` to skip reachability check

**Upload / Fetch:**
- Upload works: file → CID confirmed
- Fetch works: CID → file confirmed (while file exists on local node)

**Propagation & replication:**
- Blobs are stored locally only — deleting removes them permanently if no other
  node has fetched the CID first
- 3 peers connected via bootstrap nodes, but peers do **not** automatically
  replicate your content
- **Single device limitation**: full sync story requires minimum 2 nodes — one
  uploading, one fetching while uploader is online

**Phase 2 open question**: find a test pinning node or ask Logos team if a public
pinning service exists for testnet.

### Key insight for logos-notes Phase 2

When integrating with storage for encrypted note backup:
- Storage is content-addressed (CID-based) — upload returns a CID, download takes a CID
- Notes module would call `storage_module` via `LogosAPIClient::invokeRemoteMethod()`
- Upload flow: `uploadInit(filepath, chunkSize)` → `uploadChunk()` → `uploadFinalize()` → get CID
- Download flow: `downloadToUrl(cid, destUrl, local, chunkSize)` → file at destination
- All operations are async via `eventResponse` signal
- Storage and notes would be separate core modules communicating via LogosAPI IPC

---

## Lessons Learned

**NotesPlugin is the only surface QML can see.**
Phase 1 multi-note methods (`createNote`, `loadNotes`, `loadNote(int)`,
`saveNote(int, text)`, `deleteNote`) were implemented in `NotesBackend`
and tested, but never exposed as `Q_INVOKABLE` on `NotesPlugin`.
`logos.callModule` calls silently fail when the method doesn't exist
on the plugin — no error, just empty/null response.

**Rule:** every method added to `NotesBackend` that QML needs must also
be explicitly added to `NotesPlugin` as `Q_INVOKABLE`. The plugin is
the only surface the QML bridge can see. This applies to both the
Logos App plugin path (`logos.callModule`) and any future direct
`Q_INVOKABLE` usage.

**`logos.callModule` returns JSON, not raw text.**
`logos.callModule("notes", "loadNote", [id])` returns the note text
as a JSON-wrapped string, not raw plaintext. QML must parse the
response before assigning to `editor.text`. Assigning the raw
`callModule` result directly to `TextEdit.text` will show JSON
instead of content. On error it returns `{"error":"..."}` — guard
against this in QML to avoid flashing error JSON in the editor.

**Rule: update README after every master merge.**
After merging feature or security branches to master, always update
README.md (features, roadmap status, security tracker) and commit
with message `docs: update README for vX.X`.

**Active DB path is `~/.local/share/logos-co/logos-notes/notes.db`.**
The standalone app uses this path (set by `setOrganizationName("logos-co")`
and `setApplicationName("logos-notes")` in `main.cpp`). The Logos App
plugin uses `~/.local/share/logos_host/notes.db` (different process).
When wiping for tests, delete both. When verifying standalone DB
contents, check `logos-co/logos-notes/`.

**Normalize mnemonic before all crypto operations.**
Raw mnemonic text from QML may have different spacing, casing, or
unicode forms. Always pass through `normalizeMnemonic()` (NFKD +
simplified whitespace + lowercase) before key derivation, fingerprint
derivation, or backup re-derivation. Without this, the same phrase
entered differently produces different master keys → data loss.

**Loader destroys QML components on screen switch.**
`main.qml` uses a `Loader` that destroys the previous screen when
`backend.currentScreen` changes. Any data in the old screen's QML
properties (e.g. `mnemonicArea.text`) is gone by the time a
`Connections.onCurrentScreenChanged` handler fires. Pass data through
the C++ backend (e.g. `backupPath` parameter on `importMnemonic()`)
instead of relying on QML state surviving screen transitions.

**Commit account state only after all operations succeed.**
`m_db.setInitialized()` must be called after backup restore (if any)
succeeds, not before. Otherwise a failed restore leaves an empty
initialized account that shows the unlock screen on next launch.

---

## Development Routines

### After every feature branch
1. Manual UI/UX test checklist before merging
2. Run: `cmake --build build && cmake --install build`
3. Test in Logos App (not just standalone)
4. Run: `ctest` (all tests must pass)
5. Merge to master
6. Update README.md — features, roadmap, screenshots
7. Create GitHub release with version tag
8. Write blog post in `blog/` — builder reflection, what was built,
   what broke, what was learned. Update `blog/README.md` index.
9. Post to X

### Security fix routine
1. Create branch: `security/pX-fixes`
2. Coder implements fixes
3. Run review loop: `./scripts/security-review-loop.sh HEAD~N "src/core/"`
4. Reviewer (Codex) reviews diff
5. Paste findings to Coder
6. Repeat until clean
7. Manual UI/UX test
8. Merge to master
9. Update SECURITY_REVIEW.md with review round history

### UI/UX test checklist (run before every merge)
- Fresh reset → import mnemonic → PIN accepted
- Write note → lock → unlock → note intact
- Multiple notes → create, switch, delete
- Close tab in Logos App → reopen → unlock → notes intact
- Wrong PIN 5x → lockout → wait → correct PIN works
- Check SQLite: `sqlite3 ~/.local/share/logos_host/notes.db "SELECT hex(title_ciphertext) FROM notes LIMIT 1;"`
  (should show hex blob, not readable text)

### Logos App updates
- Check for logos-app-poc updates weekly:
  `cd ~/logos-app && git fetch && git log HEAD..origin/master --oneline`
- If updates exist, pull and rebuild:
  `git pull && nix build`
- Re-test notes module after every Logos App update:
  run full UI/UX checklist
- Watch for changes to:
  - `PluginInterface` (breaking changes to module contract)
  - `LogosQmlBridge` (new methods or behavior changes)
  - `logos-design-system` (token changes affecting hardcoded colors)
- If `PluginInterface` changes — update `NotesPlugin` immediately

### Branch naming
- `feature/description` — new features
- `security/pX-fixes` — security fixes by priority
- Always delete merged branches locally and remotely

---

## Notes for Claude Code Sessions

When starting a new Claude Code session:
1. Read this file first (`cat CLAUDE.md`)
2. Check current status checklist above
3. Explore relevant reference before writing code:
   - `~/logos-app/src/` for shell integration patterns
   - `~/status-desktop/ui/imports/shared/popups/addaccount/` for seed phrase UX
   - `~/logos-app/src/qml/` for QML theme/component patterns
4. Ask before deviating from Phase 0 scope
5. Prefer simple and correct over clever and broken
