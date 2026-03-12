# logos-notes

Encrypted, local-first notes app for the [Logos](https://logos.co) ecosystem.

Built as a Qt6/QML desktop application targeting the Logos App (Basecamp) module host. Phase 0 ships as a standalone app; later phases integrate with Logos Messaging and Logos Storage.

**Approved idea & detailed roadmap**: [logos-co/ideas#13](https://github.com/logos-co/ideas/issues/13)

---

## What it does

- **Multiple encrypted notes** with a sidebar for navigation
- Enter a BIP39 recovery phrase once to derive your encryption key
- Set a PIN that protects access across sessions — the mnemonic is never stored
- Create, edit, and delete notes — each is AES-256-GCM encrypted before hitting disk
- Auto-save per note on a 1.5s timer
- Sidebar shows note title (first line) and relative timestamp
- Lock/unlock flow wipes the master key from memory on lock
- Single encrypted SQLite database — no plaintext on disk

No accounts, no servers, no plaintext on disk.

---
## Phase 0 Screencast
https://github.com/user-attachments/assets/59ef5b7b-d02e-4a77-97e0-a629ba17ec28

---
## Screenshots

Running inside [Logos App](https://github.com/logos-co/logos-app-poc) (Basecamp):

| Import | Unlock |
|--------|--------|
| ![Import](Assets/Screenshots/import.png) | ![Unlock](Assets/Screenshots/Unlock.png) |

| Multi-note sidebar (Phase 1) |
|-------------------------------|
| ![Multi-note sidebar](Assets/Screenshots/Phase2_Multinote.png) |

---

## Security model

Your recovery phrase is the root of everything. When you first import it, the app derives a master encryption key from it and then forgets the phrase entirely. It is never written to disk, never stored in a database, never logged. If you lose your recovery phrase, there is no way to recover your notes on a new device.

The PIN exists to protect day-to-day access. During import, the app encrypts your master key with a key derived from your PIN and stores that encrypted bundle in the local database. On every subsequent launch, you enter your PIN to unwrap the master key back into memory. A wrong PIN fails the decryption — there is no hint or retry limit, just a cryptographic pass-or-fail check.

When you lock the app, the master key is wiped from memory immediately. The encrypted note and the PIN-wrapped master key remain on disk, but neither is readable without the PIN. Nothing sensitive lives in memory while the app is locked.

An attacker with access to your device would find only encrypted blobs in the SQLite database. To read your notes, they would need your PIN to unwrap the master key, or your original recovery phrase to re-derive it. Without either, the AES-256-GCM ciphertext is computationally infeasible to break.

---

## Encryption

```
BIP39 mnemonic
    └─ Argon2id (deterministic salt = SHA-256(mnemonic))
           └─ 256-bit master key  (never stored)

PIN
    └─ Argon2id (random salt, stored in DB)
           └─ 256-bit wrapping key
                  └─ AES-256-GCM(master key)  → stored in DB

Note content
    └─ AES-256-GCM(plaintext, master key, random nonce)  → stored in DB
```

Wrong PIN → GCM authentication tag fails → access denied.
The mnemonic is only entered once (import). All subsequent unlocks use the PIN alone.

---

## Screens

| Screen | Shown when |
|--------|-----------|
| **Import** | First launch — enter recovery phrase + set PIN |
| **Unlock** | Every subsequent launch — enter PIN |
| **Notes** | After unlock — sidebar with note list + auto-saving editor |

---

## Tech stack

| Component | Technology |
|-----------|-----------|
| Language | C++17 |
| UI | Qt 6.9.3 / QML / QtQuick Controls |
| Crypto | libsodium 1.0.18 (AES-256-GCM, Argon2id) |
| Storage | SQLite via Qt SQL |
| Build | CMake 3.28 + Ninja |
| Nix | flake.nix for reproducible builds and dev shell |

---

## Building

### Prerequisites

- Qt 6.6+ (tested with 6.9.3) — install via Qt online installer or system packages
- `libsodium-dev`, `cmake`, `ninja-build`, `pkg-config`

```bash
# Ubuntu
sudo apt install libsodium-dev cmake ninja-build pkg-config
```

### Configure and build

```bash
cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=~/Qt/6.9.3/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build
```

### Run standalone

```bash
./build/logos-notes
```

### Install as Logos App module

```bash
cmake -DCMAKE_INSTALL_PREFIX=$HOME -P build/cmake_install.cmake
```

This installs:
- `notes_plugin.so` → `~/.local/share/Logos/LogosApp/modules/notes/`
- `notes_ui` QML plugin → `~/.local/share/Logos/LogosApp/plugins/notes_ui/`

Then launch the Logos App and click **Load** on `notes_ui` in the UI Modules tab.

### Nix dev shell

```bash
nix develop   # drops into a shell with Qt6, libsodium, cmake, ninja, clangd
```

---

## Project structure

```
logos-notes/
├── CMakeLists.txt
├── flake.nix
├── modules/notes/manifest.json        # Core module manifest
├── plugins/notes_ui/
│   ├── manifest.json                  # UI plugin manifest
│   ├── metadata.json                  # QML plugin metadata
│   └── Main.qml                      # Module UI (all 3 screens)
└── src/
    ├── main.cpp                       # Standalone app entry point
    ├── core/
    │   ├── CryptoManager.h/cpp        # AES-256-GCM + Argon2id (libsodium)
    │   ├── DatabaseManager.h/cpp      # SQLite: notes, wrapped_key, meta
    │   ├── KeyManager.h/cpp           # BIP39 validation, in-memory key lifecycle
    │   └── NotesBackend.h/cpp         # Screen navigation, note persistence
    ├── plugin/
    │   ├── NotesPlugin.h/cpp          # PluginInterface for Logos App
    │   └── plugin_metadata.json       # Embedded Qt plugin metadata
    └── ui/
        ├── main.qml
        ├── screens/
        │   ├── ImportScreen.qml
        │   ├── UnlockScreen.qml
        │   └── NoteScreen.qml
        └── components/
            └── PinInput.qml
```

---

## Roadmap

| Phase | Goal |
|-------|------|
| **0** ✓ | Standalone encrypted notes app + Logos App module — import, unlock, write, lock |
| **1** ✓ | Multiple notes with sidebar UI, CRUD, auto-save, delete, title extraction |
| **2** | Swap Argon2 key derivation → Keycard hardware key (same PIN UX, same DB schema) |
| **3** | Logos Storage backup + Logos Messaging sync across devices |

See [logos-co/ideas#13](https://github.com/logos-co/ideas/issues/13) for the detailed roadmap and phase definitions.

---

## Related repositories

| Repo | Purpose |
|------|---------|
| [logos-app-poc](https://github.com/logos-co/logos-app-poc) | Shell host that loads this module |
| [logos-chat-ui](https://github.com/logos-co/logos-chat-ui) | Reference dapp (same C++/QML pattern) |
| [logos-cpp-sdk](https://github.com/logos-co/logos-cpp-sdk) | C++ SDK for Logos modules |
| [logos-docs](https://github.com/logos-co/logos-docs) | Ecosystem documentation |
