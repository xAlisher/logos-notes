# Building Immutable Notes on Logos

*Originally drafted March 15, 2026. Updated March 17 with v1.0.0 Keycard release.*

## Why This Should Exist on Logos

Most note-taking apps are surveillance by default. Your thoughts live on someone else's server, readable by the company, accessible to governments, vulnerable to breaches. Even "encrypted" apps often hold the keys themselves.

Logos is built around a different premise. No central servers. No company holding your keys. No permission required to participate. A notes app on Logos means your notes exist only for you, encrypted by a key that only you hold, with no intermediary ever seeing the plaintext.

What makes v1.0.0 interesting beyond a single app: **any Logos app can be unlocked by a Keycard.** Notes, wallet, chat — one card, one PIN, keys never leave the chip. Pull the card out and the session ends. No passwords, no keys on disk. Domain separation ensures each app derives its own key from the same card. This is what hardware-backed identity looks like for a modular ecosystem.

[Idea and development phases](https://github.com/logos-co/ideas/issues/13) | [Repository](https://github.com/xAlisher/logos-notes)

| Create with Keycard | Note editor |
|---------------------|-------------|
| ![Create](../Assets/Screenshots/1.0.0/create.png) | ![Notes](../Assets/Screenshots/1.0.0/notes.png) |

---

## v0.1.0 to v1.0.0 in Five Days

| Version | What shipped | Day |
|---------|-------------|-----|
| v0.1.0 | Single encrypted note, Logos App module | 1 |
| v0.2.0 | Multiple notes, sidebar | 1 |
| v0.3.0 | Security hardening (BIP39 validation, random salt, PIN lockout) | 2 |
| v0.4.0 | P2 security, AES-NI fail-fast | 2 |
| v0.5.0 | Settings, encrypted backup/restore, deterministic identity | 3 |
| v0.6.0 | LGX packaging, 95-case test suite | 3 |
| **v1.0.0** | **Keycard hardware key derivation, UI redesign** | **5** |

Built with Claude Code (Opus) for implementation and OpenAI Codex for security review. Every feature branch went through multiple review rounds before merge.

---

## How the Keycard Integration Works

Status Keycard is an ISO 7816 smart card that stores BIP-32 HD keys in tamper-resistant hardware. The host never sees the master seed — the card derives child keys internally and exports only what you ask for. Communication happens over PC/SC (the standard smart card protocol) through an encrypted secure channel.

### The library stack

```
Keycard hardware (ISO 7816 applet)
  → USB reader (PC/SC protocol)
    → pcscd daemon (Linux)
      → libpcsclite.so.1 (client library)
        → libkeycard.so (status-keycard-go, compiled via CGO)
          → KeycardBridge (our C++ wrapper, JSON-RPC)
            → NotesBackend (Qt/C++)
              → QML UI via logos.callModule()
```

We compile [status-keycard-go](https://github.com/status-im/keycard-go) as a C shared library:

```bash
cd status-keycard-go/shared
CGO_ENABLED=1 go build -buildmode=c-shared -o libkeycard.so .
```

This gives us three C functions: `KeycardInitializeRPC()`, `KeycardCallRPC(payload)`, and `KeycardSetSignalEventCallback(cb)`. All card operations go through JSON-RPC over these functions.

### Card state machine

The Go library runs a background goroutine that monitors PC/SC and maintains a state machine:

```
Unknown → WaitingForReader → WaitingForCard → ConnectingCard → Ready → Authorized
                                                                  ↓
                                              NotKeycard / EmptyKeycard / BlockedPIN
```

We poll `keycard.GetStatus` every 500ms via RPC. The QML renders two-line status indicators: reader state + card state, with colored dots (green/gray-blinking/red).

### Key derivation path

The Keycard applet supports BIP-32 HD key derivation. We export the encryption key at the EIP-1581 path:

```
m/43'/60'/1581'/1'/0    (EIP-1581 encryption key)
```

The card returns a 32-byte secp256k1 private key. This is an ECDSA key, not directly usable for AES. We apply domain separation:

```
SHA256(secp256k1_private_key || "logos-notes-encryption") → 256-bit AES-256-GCM key
```

The domain tag `"logos-notes-encryption"` ensures that a different app using the same card and same derivation path gets a completely different symmetric key. If we later build a Keycard-backed wallet or chat module, each derives its own isolated key from the same hardware.

### What the APDU sequence looks like

Under the hood, `status-keycard-go` sends these ISO 7816 APDUs:

1. **SELECT** (INS 0xA4) — select the Keycard applet by AID
2. **PAIR** (INS 0x12) — two-step ECDH handshake using a pairing password (PBKDF2-SHA256, 50k iterations)
3. **OPEN SECURE CHANNEL** (INS 0x10) — ECDH key agreement, derives AES-256-CBC session keys
4. **MUTUALLY AUTHENTICATE** (INS 0x11) — confirms both sides share session keys
5. **VERIFY PIN** (INS 0x20) — PIN sent encrypted inside the secure channel
6. **EXPORT KEY** (INS 0xC2) — derives key at BIP-32 path, returns private + public key

All commands after pairing are AES-CBC encrypted with CMAC authentication. The card enforces that private key export is only allowed for EIP-1581 paths — you can't export wallet keys, only encryption/messaging keys.

The card tracks PIN attempts natively (3 max before block, PUK to unblock). We surface the remaining attempts in the UI: "Wrong PIN. 2 attempts left."

### Identity verification on unlock

On import, we store a fingerprint derived from the master key:

```
SHA256(master_key) → Ed25519 seed → public key → first 8 bytes hex
```

On every unlock, we re-derive this fingerprint from the freshly exported key and compare. Different card = different key = different fingerprint = rejected before any notes are touched.

| Unlock notes | Settings |
|--------------|----------|
| ![Unlock](../Assets/Screenshots/1.0.0/unlock.png) | ![Settings](../Assets/Screenshots/1.0.0/settings.png) |

---

## Encryption Architecture

```
Keycard:
    secp256k1 key at m/43'/60'/1581'/1'/0
        → SHA256(key || "logos-notes-encryption")
            → 256-bit AES-256-GCM master key (in memory only)

Recovery Phrase (legacy):
    BIP39 mnemonic → NFKD normalize → Argon2id (random salt, MODERATE)
        → 256-bit master key
    PIN → Argon2id → wrapping key → AES-256-GCM(master key) → stored in DB

Notes:
    → AES-256-GCM(plaintext, master key, fresh random nonce) → ciphertext + nonce in SQLite
    → Titles encrypted separately with their own nonce
```

In Keycard mode there is no wrapped key on disk — the key is derived from the card on every unlock. Remove the card, the key is wiped via `sodium_memzero`, and there's nothing on disk to attack.

---

## Building for Logos App

The Logos App loads modules dynamically. We ship two:

- **`notes`** (type: `core`) — C++ shared library implementing `PluginInterface`. Handles crypto, SQLite, Keycard bridge. Every method QML needs is `Q_INVOKABLE` on `NotesPlugin`.
- **`notes_ui`** (type: `ui_qml`) — single QML file with all screens. Calls the backend via `logos.callModule("notes", "method", [args])`, synchronous, returns JSON.

### What we learned the hard way

**Go callbacks don't work in logos_host.** The Go library offers push signals via a C function pointer. They work standalone. Inside the Logos App plugin host process, the Go goroutine thread never reaches our code. We use RPC polling instead — slightly less efficient, completely reliable.

**JSON-RPC params must always be present.** The Go framework returns `{"result":null}` when the `"params"` field is missing, even for no-arg methods. Always send `"params":[{}]`. This one bug cost hours of debugging — `Authorize` worked (has params), `ExportLoginKeys` returned null (no params).

**AppImage sandboxes system libraries.** `logos_host` inside the AppImage can't find system `libpcsclite.so.1`. We bundle it alongside our plugin with `$ORIGIN` RPATH. CMake `add_library(IMPORTED)` embeds absolute build paths — use `link_directories()` + link by name instead.

**QML sandbox restrictions.** No `FileDialog`, no `Logos.Theme` import, no native file I/O. Hardcode the palette, move file operations to C++, build custom controls. SVG icons work via `Image { source: "file.svg" }` but need `z: 10` on the `MouseArea` to receive clicks.

Full technical deep-dive: [Keycard Integration: Hardware Keys for Encrypted Notes](2026-03-17-keycard-integration.md)

---

## Two-AI Security Review

Every feature goes through a review loop: Claude Code implements, Codex reviews the diff, findings are addressed, re-reviewed until LGTM.

This caught a cipher fallback bug (AES-GCM → XChaCha20 without persisting which cipher was used — move the DB to a different machine and your notes become unreadable). It caught wrong-card acceptance at unlock. It caught incomplete install artifacts that broke staged builds.

The Keycard branch alone went through four review rounds. Full audit: [SECURITY_REVIEW.md](https://github.com/xAlisher/logos-notes/blob/master/SECURITY_REVIEW.md).

---

## What's Next

**v1.1.0 — Shared Keycard module.** This is the real payoff. Extract `KeycardBridge` into a standalone ecosystem module. Wallet, chat, notes, any Logos app — one card, one PIN, all unlocked. The card becomes your identity across the platform. No passwords anywhere.

**Trust Network.** Encrypted backup with redundancy through trusted peers. Export to Logos Storage, broadcast CID via Waku to peers who opted in. Reciprocal — both sides must add each other.

**Card initialization.** Set up a blank Keycard from within the app. Currently requires Keycard Shell or Status Desktop.

---

## Try It

```bash
git clone https://github.com/xAlisher/logos-notes
cd logos-notes
./scripts/build-libkeycard.sh
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.9.3/gcc_64
cmake --build build -j4 && cmake --install build
```

Requirements: USB smart card reader, Status Keycard (pre-initialized), `pcscd` daemon.

[github.com/xAlisher/logos-notes](https://github.com/xAlisher/logos-notes) | [v1.0.0 release](https://github.com/xAlisher/logos-notes/releases/tag/v1.0.0)

---

*"We must defend our own privacy if we expect to have any."*
— Eric Hughes, A Cypherpunk's Manifesto, 1993
