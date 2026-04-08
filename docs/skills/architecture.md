# Architecture Reference

> Consult this file on-demand during implementation. Not required at session start.

---

## Encryption

Two key derivation paths coexist. The `key_source` meta field (`"mnemonic"` or `"keycard"`)
determines which unlock flow is used.

### Path 1: Mnemonic + PIN (local key derivation)

```
BIP39 mnemonic
  → NFKD normalize (normalizeMnemonic())
  → Argon2id (random persisted salt, OPSLIMIT_MODERATE)
  → 256-bit master key (never stored raw)

PIN
  → Argon2id (random salt, OPSLIMIT_MODERATE)
  → PIN wrapping key
  → AES-256-GCM(master key) → wrapped key blob stored in SQLite

Unlock: PIN → re-derive wrapping key → decrypt wrapped_key → master key in memory
```

### Path 2: Keycard module (external key derivation)

```
keycard-basecamp module (external, via logos.callModule)
  → QML calls logos.callModule("keycard", "requestAuth", ["notes_encryption", "notes"])
  → polls logos.callModule("keycard", "checkAuthStatus", [authId])
  → receives 256-bit hex key from card
  → passed to importWithKeycardKey() or unlockWithKeycardKey()
  → master key held in memory (no wrapped_key stored)

Unlock: card re-derives key → fingerprint verified against stored → master key in memory
```

> **Historical note (pre-v1.2.0):** Keycard integration originally used an internal
> `KeycardBridge` wrapping `libkeycard.so` (Go `status-keycard-go` compiled as shared lib).
> Epic #62 replaced this with the external keycard-basecamp module via LogosAPI IPC.
> The runtime no longer uses KeycardBridge, libkeycard.so, Go JSON-RPC, or direct PC/SC code.
> All legacy artifacts have been removed from the repo.

### Common encryption (both paths)

```
Note content + title
  → AES-256-GCM(plaintext, master key, random nonce)
  → ciphertext + nonce stored in SQLite
  → plaintext never touches disk

Account identity (mnemonic path)
  → SHA-256(normalized_mnemonic) → Ed25519 seed
  → crypto_sign_seed_keypair() → public key
  → full 64-char uppercase hex = account fingerprint
  → deterministic, stable across devices and re-imports

Account identity (keycard path)
  → SHA-256(master_key) → Ed25519 seed
  → crypto_sign_seed_keypair() → public key
  → first 16 hex characters = account fingerprint
```

## SQLite Schema

```sql
notes(id, ciphertext, nonce, title_ciphertext, title_nonce, updated_at)
wrapped_key(id, ciphertext, nonce, pin_salt)
meta(key, value)
-- meta keys: initialized, mnemonic_kdf_salt, account_fingerprint,
--            pin_failed_attempts, pin_lockout_until, backup_cid, key_source
```

## DB Paths

| Context | Path |
|---------|------|
| Logos Basecamp plugin | `~/.local/share/logos_host/notes.db` |
| Standalone app | `~/.local/share/logos-co/logos-notes/notes.db` |

When wiping for tests, delete both. When verifying DB contents, check which context you're in.

## Logos Basecamp Module Structure

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

## Plugin Contract (Q_INVOKABLE Surface)

```cpp
// Shell lifecycle
Q_INVOKABLE void    initLogos(LogosAPI* api);
Q_INVOKABLE QString initialize();
Q_INVOKABLE QString isInitialized();

// Account management — mnemonic/PIN path
Q_INVOKABLE QString importMnemonic(const QString& mnemonic, const QString& pin,
                                   const QString& confirm, const QString& backupPath = {});
Q_INVOKABLE QString unlockWithPin(const QString& pin);

// Account management — keycard module path
Q_INVOKABLE QString importWithKeycardKey(const QString& hexKey, const QString& backupPath = {});
Q_INVOKABLE QString unlockWithKeycardKey(const QString& hexKey);

// Note CRUD
Q_INVOKABLE QString createNote();
Q_INVOKABLE QString loadNotes();
Q_INVOKABLE QString loadNote(int id);
Q_INVOKABLE QString saveNote(int id, const QString& text);
Q_INVOKABLE QString deleteNote(int id);

// Session & account
Q_INVOKABLE QString lockSession();
Q_INVOKABLE QString getKeySource();
Q_INVOKABLE QString getAccountFingerprint();
Q_INVOKABLE QString resetAndWipe();

// Backup
Q_INVOKABLE QString exportBackup(const QString& filePath);
Q_INVOKABLE QString exportBackupAuto();
Q_INVOKABLE QString listBackups();
Q_INVOKABLE QString importBackup(const QString& filePath, const QString& mnemonic = {});
```

## QML Bridge

`logos.callModule(module, method, args)` — synchronous, returns JSON string.

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

**Resolved**: `initLogos(LogosAPI*)` is the correct signature. It is called reflectively
via `QMetaObject::invokeMethod`, not through virtual dispatch (see lesson #19).

## Logos Core C API (How the Shell Loads Modules)

```c
logos_core_set_plugins_dir(dir);        // tell core where .so files live
logos_core_start();                     // start core process
logos_core_load_plugin("notes");        // load our plugin
logos_core_call_plugin_method_async(    // call a method with JSON params
    "notes", "initialize", "[]", callback, userData);
logos_core_register_event_listener(    // subscribe to eventResponse signal
    "notes", "eventResponse", callback, userData);
```

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
| `notes.lgx` | core | `manifest.json` + `variants/linux-amd64/notes_plugin.so` + bundled deps (libsodium) |
| `notes_ui.lgx` | ui_qml | `manifest.json` + `variants/linux-amd64/Main.qml` + `metadata.json` |

### Post-install workaround

No post-install workarounds needed. Previous `fix-lgx` script (for removing bundled
`libpcsclite.so`) was removed — keycard-basecamp module handles card communication externally.

### Current state

✅ **LGX packaging complete** (issue #44, 2026-03-19)
- Core and UI modules install via Package Manager
- Portable bundler generates correct `linux-amd64` platform

`cmake --install` still works for AppImage-based testing.

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

### ChatSDK Q_INVOKABLE Methods (Reference for IPC Patterns)

```
connect(url)
disconnect()
sendMessage(roomId, text)
joinRoom(roomId)
leaveRoom(roomId)
getMessages(roomId)
getRooms()
```
