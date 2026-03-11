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

# Run standalone (Phase 0)
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

## Key Contacts / Links

- Ideas issue: https://github.com/logos-co/ideas/issues/13
- Logos App PoC: https://github.com/logos-co/logos-app-poc
- Chat UI reference: https://github.com/logos-co/logos-chat-ui
- Waku docs (messaging, Phase 2): https://docs.waku.org
- Logos Storage (Phase 2): https://github.com/logos-storage/logos-storage-nim

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
