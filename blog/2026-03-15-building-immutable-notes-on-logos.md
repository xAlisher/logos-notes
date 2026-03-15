# Building Immutable Notes on Logos

## Why This Should Exist on Logos

Most note-taking apps are surveillance by default. Your thoughts live on someone else's server, readable by the company, accessible to governments, vulnerable to breaches. Even "encrypted" apps often hold the keys themselves.

Logos is built around a different premise entirely. No central servers. No company holding your keys. No permission required to participate. A notes app on Logos is an act of thought sovereignty. Your notes exist only for you, encrypted by a key that only you hold, with no intermediary ever seeing the plaintext.

The full vision is an encrypted notes manager with Keycard hardware key protection and sync across devices via Logos Messaging and Logos Storage. No accounts. No servers. Your recovery phrase is your identity.

More about the idea and all development phases is [here](https://github.com/logos-co/ideas/issues/13).

I started building this four days ago. It now has multi-note support, a full security audit, encrypted backups, and runs both as a standalone desktop app and inside Logos App. This post covers how it all fits together and what I learned building for the Logos platform.

<!-- IMAGE: Side-by-side screenshot — standalone app on left, same module inside Logos App on right. Shows the multi-note sidebar with a few notes. -->

---

## Getting Started

After the [Logos ideas repo](https://github.com/logos-co/ideas) opened for community contributions, I looked for something lean enough to actually build but meaningful enough to matter. Encrypted notes with Keycard + Logos sync felt right — personal sovereignty, hardware security, decentralized infrastructure, all in one small app.

I started by cloning everything relevant:

- [logos-app-poc](https://github.com/logos-co/logos-app-poc) — the Logos App shell, the Qt6/C++ host that loads modules
- [logos-chat-ui](https://github.com/logos-co/logos-chat-ui) — primary reference dapp, same C++/QML pattern
- [logos-template-module](https://github.com/logos-co/logos-template-module) — scaffold starting point
- [logos-design-system](https://github.com/logos-co/logos-design-system) — QML components and theme tokens
- [status-desktop](https://github.com/status-im/status-desktop) — the most mature app on this stack, invaluable for QML patterns

Environment: Ubuntu 24.04, Qt 6.9.3, CMake 3.28, libsodium 1.0.18, Nix.

For building and research I used Claude Code. The collaboration felt like a hackathon — I brought the roadmap, the UX direction, and the architectural decisions. Claude Code brought speed and the ability to read fifty source files and write correct C++ in the time it takes to make coffee. The discipline that made it work: explore first, understand the contracts, then implement. For security review I used a two-AI loop — Claude writes code, Codex reviews the diffs. More on that below.

---

## What It Does Today

<!-- IMAGE: Import screen with recovery phrase field (showing "••• 12 words entered •••" masked state) and PIN fields -->

**Import once.** Enter your BIP39 recovery phrase and set a PIN. The app validates your phrase against the full 2048-word BIP39 wordlist with checksum verification. It derives your encryption key, wraps it with your PIN, and forgets the phrase forever. It never touches disk.

**Write freely.** Create as many notes as you want. Each one is AES-256-GCM encrypted before hitting the database. Titles are encrypted too — not just the body. A sidebar shows your notes with relative timestamps. Auto-save every 1.5 seconds.

<!-- IMAGE: Note screen showing sidebar with multiple notes and editor area -->

**Lock and unlock.** When you lock, `sodium_memzero` wipes the master key from memory. When you unlock, your PIN re-derives the wrapping key and decrypts. Wrong PIN? AES-GCM authentication tag fails. No partial results, no hints.

**PIN protection.** Five wrong attempts and you're locked out with an exponential backoff timer — 30 seconds, then 60, 120, 5 minutes, 10 minutes. The countdown ticks live in the UI. The counter persists across app restarts.

<!-- IMAGE: Unlock screen showing lockout countdown timer ("Locked (25s)") -->

**Your identity is your phrase.** The app derives an Ed25519 public key from your mnemonic — deterministic, same key on any device, with any PIN. This is your identity in Logos Notes. Visible in Settings, shown on the unlock screen.

<!-- IMAGE: Settings screen showing full public key, Export Backup button, Danger Zone -->

**Encrypted backups.** Export all your notes as a single `.imnotes` file — encrypted with your master key, portable to any device. Import on a new machine by entering the same recovery phrase. Different PIN is fine. The backup includes the KDF salt so the key can be re-derived.

<!-- VIDEO (optional, 30s): Quick demo — export backup → remove account → import phrase → restore from backup → notes are back -->

---

## The Crypto Architecture

One principle: **nothing sensitive ever touches disk in plaintext.**

```
BIP39 mnemonic
    └─ normalize (NFKD, lowercase, whitespace)
    └─ Argon2id (random persisted salt, OPSLIMIT_MODERATE)
           └─ 256-bit master key (never stored)

PIN (min 6 characters)
    └─ Argon2id (random salt, OPSLIMIT_MODERATE)
           └─ 256-bit wrapping key
                  └─ AES-256-GCM(master key) → stored in DB

Note content + title
    └─ AES-256-GCM(plaintext, master key, random nonce) → stored in DB

Identity
    └─ SHA-256(normalized mnemonic) → Ed25519 seed
           └─ Public key (deterministic, same phrase = same key always)
```

An attacker with full disk access finds no plaintext notes or mnemonic — only encrypted blobs, the PIN-wrapped master key, and KDF salts. The database has `PRAGMA secure_delete=ON` and `0600` file permissions.

All temporary key material (PIN-derived keys, intermediate buffers, mnemonic UTF-8 bytes) is wrapped in a [SecureBuffer](https://github.com/xAlisher/logos-notes/blob/master/src/core/SecureBuffer.h) RAII class that calls `sodium_memzero` on destruction.

---

## How Logos App Modules Work

This was the most valuable discovery. The Logos App is a microkernel that loads modules dynamically. Three types:

| Type | Mechanism | Use case |
|------|-----------|----------|
| `core` | C++ `.so` implementing `PluginInterface` | Backend logic, crypto, storage |
| `ui` | C++ `.so` implementing `IComponent` | Full UI with push events |
| `ui_qml` | Plain `.qml` file, no C++ needed | Simple UI, synchronous calls only |

We built two modules:
- `notes` (type: `core`) — [NotesPlugin.cpp](https://github.com/xAlisher/logos-notes/blob/master/src/plugin/NotesPlugin.cpp) wraps the backend
- `notes_ui` (type: `ui_qml`) — [Main.qml](https://github.com/xAlisher/logos-notes/blob/master/plugins/notes_ui/Main.qml), all screens in one file

**The QML bridge is synchronous-only.** `logos.callModule("notes", "methodName", [args])` blocks, returns a JSON string, done. No signals, no push events for `ui_qml` type. For notes this is sufficient — everything is user-initiated.

**Installation layout:**
```
~/.local/share/Logos/LogosApp/modules/notes/
├── manifest.json
└── notes_plugin.so

~/.local/share/Logos/LogosApp/plugins/notes_ui/
├── manifest.json
└── Main.qml
```

---

## Lessons for Other Builders

Four days of building taught me more about the Logos platform than any documentation could. Here are the things I wish I'd known on day one.

### The plugin surface rule

`NotesPlugin` is the **only** surface QML can see. If you add a method to your backend but don't expose it as `Q_INVOKABLE` on the plugin class, `logos.callModule` silently returns null. No error, no warning. You'll spend an hour debugging a typo.

**Rule:** every backend method that QML needs must be explicitly exposed on the plugin. Check twice.

### The sandbox is real

`ui_qml` plugins run in a sandboxed `QQuickWidget`. Things that work in standalone don't work inside:

- **`FileDialog`** — silently does nothing. Use fixed well-known paths or move file I/O to C++ plugin.
- **`import Logos.Theme`** — fails silently. Hardcode the dark palette hex values.
- **`CheckBox`** — renders as an unstyled white square. Build a custom one.
- **`StandardPaths`** — may not resolve correctly. Get paths from C++ side.

The `storage_ui` module avoids all of this by being a compiled C++ `.so` plugin with QML embedded. If you need native dialogs or full Qt access, go `type: "ui"` instead of `ui_qml`.

### QML syntax errors are invisible

If your plugin QML has a syntax error, Logos App silently fails to load it. No error message in the UI. The journal log shows the error, but you have to know to look. A dangling brace cost me an hour.

**Rule:** run `qmllint` on every QML change before installing:
```bash
~/Qt/6.9.3/gcc_64/bin/qmllint plugins/notes_ui/Main.qml
```

### Kill everything between tests

Logos App spawns a `logos_host` child process per module. These survive the parent process being killed and hold stale `.so` files. If Load does nothing, it's probably a zombie from a previous session.

```bash
kill -9 $(ps aux | grep logos | grep -v grep | awk '{print $2}')
```

### Always test in both environments

Standalone testing covers about 60% of bugs. The other 40% only appear inside Logos App — different QML engine, different available components, different file system access. Test both before every merge.

### Normalize everything before crypto

BIP39 validation normalizes the mnemonic (NFKD, whitespace, lowercase). But if your key derivation and fingerprint code use the raw string, the same twelve words entered with different spacing produce different keys. Backup becomes unrestorable. One shared `normalizeMnemonic()` function, called before every crypto operation.

---

## The Two-AI Security Review

After shipping the core features, I ran a full security audit using two AI systems against each other. Claude Code (Opus) wrote the fixes. OpenAI Codex reviewed the diffs. I sat in the middle deciding what to act on.

The workflow:
1. Claude implements a security fix
2. Git diff is piped to Codex via a [review script](https://github.com/xAlisher/logos-notes/blob/master/scripts/security-review-loop.sh)
3. Codex reviews for crypto correctness, memory safety, input validation
4. Findings go back to Claude for fixes
5. Repeat until clean

This caught real bugs. The most serious: a cipher fallback (AES-GCM → XChaCha20) that wasn't persisted with the encrypted data. If you encrypted on a machine with AES-NI and moved the database to one without it, your data became unreadable. Codex flagged it as High severity. We stripped the fallback entirely — AES-NI has been standard since 2010, dual-cipher complexity wasn't worth the edge case.

Four review rounds, ten findings, all resolved or documented as known limitations. Full audit in [SECURITY_REVIEW.md](https://github.com/xAlisher/logos-notes/blob/master/SECURITY_REVIEW.md).

---

## What's Next

**Keycard hardware key.** Swap Argon2id software key derivation for Keycard hardware key. Same PIN UX, same SQLite schema, no data migration.

**Trust Network.** Automatic encrypted backup with redundancy through trusted peers. Export your encrypted blob to Logos Storage, broadcast the CID via Logos Messaging to peers who opted in. Reciprocal by design — both sides must add each other. No central server, no managed storage. [Design spec here.](https://github.com/xAlisher/logos-notes/issues/15)

**LGX package.** Package as a proper `.lgx` module for installation via the Logos App Package Manager — so anyone can install it with one click.

**AppImage packaging.** Currently blocked on a Qt QML AOT compilation issue. The Nix-based approach matching Logos App's own packaging is the likely path.

---

## Try It

The app is open source and runs today — both standalone and inside Logos App.

```bash
git clone https://github.com/xAlisher/logos-notes
cd logos-notes
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.9.3/gcc_64
cmake --build build
./build/logos-notes
```

Twenty-five tests. Five versions shipped. Six blog posts. Zero plaintext on disk.

Clone it, break it, build on top of it. Find me on [Status](https://status.app/u/CwmAChEKD0FsaXNoZXIgU2hlcmFsaQM=#zQ3shWBWbQjMhpevjRT3KifqunFR8F81hbwzRMs7193PgWrhf) or in the Logos Discord.

[github.com/xAlisher/logos-notes](https://github.com/xAlisher/logos-notes)

---

*"We must defend our own privacy if we expect to have any."*
— Eric Hughes, A Cypherpunk's Manifesto, 1993
