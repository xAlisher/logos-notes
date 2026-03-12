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
- [ ] Register as Logos App module (metadata.json + plugin interface)
- [ ] Update https://github.com/logos-co/ideas/issues/13 with Phase 0 definition

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
