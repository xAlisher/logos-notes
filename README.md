# logos-notes

Encrypted, local-first notes app for the [Logos](https://logos.co) ecosystem.

Hardware-secured encryption via [Status Keycard](https://keycard.tech). No accounts. No servers. Your card is the key.

**Approved idea & detailed roadmap**: [logos-co/ideas#13](https://github.com/logos-co/ideas/issues/13)

---

## Screenshots (v1.0.0)

| Create new database | Unlock notes |
|---------------------|--------------|
| ![Create](Assets/Screenshots/1.0.0/create.png) | ![Unlock](Assets/Screenshots/1.0.0/unlock.png) |

| Note editor | Settings |
|-------------|----------|
| ![Notes](Assets/Screenshots/1.0.0/notes.png) | ![Settings](Assets/Screenshots/1.0.0/settings.png) |

---

## What it does

### Keycard (v1.0.0)
- **Import with Keycard**: plug in USB reader, insert card, enter PIN — notes encrypted
- **Unlock with Keycard**: card must be present every time — PIN verifies, key derives, notes decrypt
- **Auto-lock on removal**: pull the card out, session locks instantly
- **Wrong card rejection**: different card = different key = "Wrong keys. Try different Keycard."
- **Domain separation**: `SHA256(secp256k1_key || "logos-notes-encryption")` — same card, different apps, different keys

### Notes
- **Multiple encrypted notes** with sidebar navigation
- Create, edit, delete — each encrypted with AES-256-GCM before hitting disk
- **Encrypted titles** — even metadata never touches disk as plaintext
- Auto-save per note (1.5s timer)
- Keyboard shortcuts: **Ctrl+N** (new note), **Ctrl+L** (lock)
- **Encrypted backup/restore** — export `.imnotes` file, restore on any device with same key source

### Legacy: Recovery Phrase
- Enter a BIP39 recovery phrase to derive your encryption key
- Set a PIN that wraps the master key across sessions
- PIN brute-force protection: 5 attempts, then exponential lockout (30s → 600s)

No plaintext on disk. No accounts. No servers.

---

## Security model

### Keycard mode
Your encryption key lives on a physical smart card. The Keycard derives a secp256k1 private key at the EIP-1581 encryption path (`m/43'/60'/1581'/1'/0`). We apply domain separation to produce a 256-bit AES key. The key exists in memory only while the card is present — remove the card, key is wiped via `sodium_memzero`.

### Recovery Phrase mode (legacy)
Your recovery phrase derives a master key via Argon2id with a random persisted salt. The key is wrapped with your PIN (also Argon2id) and stored encrypted in SQLite. Wrong PIN fails the AES-256-GCM auth tag. The phrase is entered once and never stored.

### Both modes
- AES-256-GCM for all encryption (notes, titles, key wrapping)
- `SecureBuffer` RAII class wipes all key material on scope exit
- Single encrypted SQLite database — only ciphertext on disk

See [SECURITY_REVIEW.md](SECURITY_REVIEW.md) for the full audit.

---

## Encryption

```
Keycard:
    secp256k1 key at m/43'/60'/1581'/1'/0
        → SHA256(key || "logos-notes-encryption")
            → 256-bit AES-256-GCM master key

Recovery Phrase (legacy):
    BIP39 mnemonic
        → Argon2id (random salt, OPSLIMIT_MODERATE)
            → 256-bit master key (never stored)
    PIN → Argon2id → wrapping key → AES-256-GCM(master key) → stored in DB

Note content + title:
    → AES-256-GCM(plaintext, master key, random nonce) → stored in DB
```

---

## Tech stack

| Component | Technology |
|-----------|-----------|
| Language | C++17 |
| UI | Qt 6.9.3 / QML |
| Crypto | libsodium 1.0.18 (AES-256-GCM, Argon2id, Ed25519) |
| Keycard | status-keycard-go (CGO → libkeycard.so) |
| Smart card | libpcsclite (PC/SC daemon) |
| Storage | SQLite via Qt SQL |
| Build | CMake 3.28 + Ninja |
| Packaging | Nix flake + LGX |

---

## Building

### Prerequisites

```bash
# Ubuntu
sudo apt install libsodium-dev cmake ninja-build pkg-config pcscd libpcsclite-dev
```

- Qt 6.6+ (tested with 6.9.3) — install via Qt online installer
- Go toolchain (for building libkeycard.so)
- USB smart card reader + Status Keycard

### Build libkeycard.so

```bash
./scripts/build-libkeycard.sh
```

This compiles `status-keycard-go` as a C shared library into `lib/keycard/libkeycard.so`.

### Configure and build

```bash
cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=~/Qt/6.9.3/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build -j4
```

### Run tests

```bash
cd build && ctest --output-on-failure   # 6 suites, 95+ test cases
```

### Install as Logos App module

```bash
cmake --install build
```

Installs:
- `notes_plugin.so` + `libkeycard.so` + `libpcsclite.so.1` → `~/.local/share/Logos/LogosApp/modules/notes/`
- `Main.qml` + icons + metadata → `~/.local/share/Logos/LogosApp/plugins/notes_ui/`

### Run in Logos App

```bash
pkill -9 -f "LogosApp.elf"; pkill -9 -f "logos_host.elf"
~/logos-app/logos-app.AppImage
```

---

## Project structure

```
logos-notes/
├── CMakeLists.txt
├── flake.nix
├── SECURITY_REVIEW.md
├── PROJECT_KNOWLEDGE.md          # Shared project memory
├── lib/keycard/                  # libkeycard.so (built from status-keycard-go)
├── scripts/
│   └── build-libkeycard.sh      # CGO build script
├── modules/notes/manifest.json
├── plugins/notes_ui/
│   ├── Main.qml                 # All screens (create, unlock, editor, settings)
│   └── metadata.json
├── assets/icons/                 # SVG icons (Add, Lock, close, app_icon)
└── src/
    ├── core/
    │   ├── NotesBackend.h/cpp    # Screen nav, note CRUD, keycard import/unlock
    │   ├── KeycardBridge.h/cpp   # C++ wrapper for libkeycard.so JSON-RPC
    │   ├── CryptoManager.h/cpp   # AES-256-GCM + Argon2id
    │   ├── DatabaseManager.h/cpp # SQLite
    │   ├── KeyManager.h/cpp      # BIP39 validation, key lifecycle
    │   └── SecureBuffer.h        # RAII key zeroization
    ├── plugin/
    │   └── NotesPlugin.h/cpp     # Logos App PluginInterface
    └── ui/                       # Standalone app screens
```

---

## Roadmap

| Version | Goal | Status |
|---------|------|--------|
| **v0.1.0** | Standalone encrypted notes + Logos App module | ✅ |
| **v0.2.0** | Multiple notes with sidebar | ✅ |
| **v0.3.0** | Security hardening (P0+P1) | ✅ |
| **v0.4.0** | P2 security fixes, AES-NI fail-fast | ✅ |
| **v0.5.0** | Settings, backup, stable identity | ✅ |
| **v0.6.0** | LGX packaging | ✅ |
| **v1.0.0** | **Keycard hardware key derivation + UI polish** | ✅ |
| **v1.1.0** | Shared keycard-module for ecosystem | Planned |
| **v2.0** | Logos Storage auto-backup | Research |
| **v3.0** | Trust Network — social backup | Proposal |

---

## Blog

| Date | Post |
|------|------|
| 2026-03-17 | [Keycard Integration: Hardware Keys for Encrypted Notes](blog/2026-03-17-keycard-integration.md) |
| 2026-03-15 | [Shared Memory: How Two AIs Stay in Sync](blog/2026-03-15-shared-memory.md) |
| 2026-03-14 | [Settings, Backup, and the Identity Question](blog/2026-03-14-settings-backup-identity.md) |
| 2026-03-14 | [Security Hardening: Two AIs Reviewing Each Other's Work](blog/2026-03-14-security-hardening.md) |
| 2026-03-12 | [Building Immutable Notes on Logos: Phase 0](blog/2026-03-12-phase-0.md) |

---

## Related

| Repo | Purpose |
|------|---------|
| [logos-app-poc](https://github.com/logos-co/logos-app-poc) | Shell host that loads this module |
| [status-keycard-go](https://github.com/status-im/keycard-go) | Go Keycard library we wrap |
| [logos-cpp-sdk](https://github.com/logos-co/logos-cpp-sdk) | C++ SDK for Logos modules |
| [logos-tutorial](https://github.com/logos-co/logos-tutorial) | Developer guide for building modules |
| [keycard.tech](https://keycard.tech/en/developers/overview) | Keycard developer docs |
