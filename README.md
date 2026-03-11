# logos-notes

Encrypted, local-first notes app for the [Logos](https://logos.co) ecosystem.

Built as a Qt6/QML desktop application targeting the Logos App (Basecamp) module host. Phase 0 ships as a standalone app; later phases integrate with Logos Messaging and Logos Storage.

**Approved idea**: [logos-co/ideas#13](https://github.com/logos-co/ideas/issues/13)

---

## What it does

- Enter a BIP39 recovery phrase once to derive your encryption key
- Set a PIN that protects access across sessions — the mnemonic is never stored
- Write a note — it is AES-256-GCM encrypted before hitting disk
- On every relaunch, enter your PIN to decrypt and resume

No accounts, no servers, no plaintext on disk.

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
| **Note** | After unlock — single auto-saving plain-text editor |

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

- Qt 6.9.3 at `~/Qt/6.9.3/gcc_64/` (or adjust `CMAKE_PREFIX_PATH`)
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

### Run

```bash
./build/logos-notes
```

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
└── src/
    ├── main.cpp
    ├── core/
    │   ├── CryptoManager.h/cpp     # AES-256-GCM + Argon2id (libsodium)
    │   ├── DatabaseManager.h/cpp   # SQLite: notes, wrapped_key, meta
    │   ├── KeyManager.h/cpp        # BIP39 validation, in-memory key lifecycle
    │   └── NotesBackend.h/cpp      # QML context property, screen navigation
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
| **0** ✓ | Standalone encrypted notes app — import, unlock, write |
| **1** | Swap Argon2 key derivation → Keycard hardware key (same PIN UX, same DB schema) |
| **2** | Logos Messaging sync — share encrypted notes across devices |

---

## Related repositories

| Repo | Purpose |
|------|---------|
| [logos-app-poc](https://github.com/logos-co/logos-app-poc) | Shell host that loads this module |
| [logos-chat-ui](https://github.com/logos-co/logos-chat-ui) | Reference dapp (same C++/QML pattern) |
| [logos-cpp-sdk](https://github.com/logos-co/logos-cpp-sdk) | C++ SDK for Logos modules |
| [logos-docs](https://github.com/logos-co/logos-docs) | Ecosystem documentation |
