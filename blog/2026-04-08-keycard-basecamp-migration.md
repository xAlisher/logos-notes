# From Bundled Library to Shared Module: The Keycard-Basecamp Migration

*April 8, 2026 — Epic #62, post-v1.2.0*

In v1.0.0, we shipped Keycard hardware key derivation by embedding `libkeycard.so` directly into the notes plugin. It worked, but it was the wrong architecture. Epic #62 ripped it out and replaced it with the shared keycard-basecamp module via Logos IPC.

This post covers what changed, why, and what we learned.

## What we had (v1.0.0–v1.1.0)

The original keycard stack was deeply embedded:

```
User inserts Keycard
  → pcscd (system daemon)
    → libkeycard.so (Go, CGO, 14MB)
      → KeycardBridge (C++ wrapper, JSON-RPC)
        → NotesBackend::importWithKeycardKey()
          → AES-256-GCM master key
```

This meant:
- `status-keycard-go` compiled as a shared library via `scripts/build-libkeycard.sh`
- A C++ `KeycardBridge` class wrapping JSON-RPC calls (`KeycardInitializeRPC`, `KeycardCallRPC`)
- `libpcsclite.so.1` bundled in the LGX to talk to the PC/SC daemon
- Post-install workaround: manually remove bundled `libpcsclite.so.1` because it broke socket connectivity
- Every Logos module that wanted keycard support would need to repeat all of this

## What we have now (v1.2.0+)

```
User inserts Keycard
  → keycard-basecamp module (separate repo, shared across ecosystem)
    → handles pcscd, card detection, PIN auth, key derivation
      → QML: logos.callModule("keycard", "requestAuth", [...])
        → polls: logos.callModule("keycard", "checkAuthStatus", [authId])
          → hex key delivered to notes plugin
            → NotesBackend::importWithKeycardKey() / unlockWithKeycardKey()
```

Notes no longer bundles any keycard library, Go code, or PC/SC dependencies. The entire keycard stack is the keycard-basecamp module's responsibility.

## Why we migrated

**1. Duplicate PC/SC code across modules.** If wallet, chat, and notes each bundle their own `libkeycard.so`, that's three copies of the same Go library fighting over the same card reader. The keycard-basecamp module handles all card communication in one place.

**2. The libpcsclite bundling problem was unsolvable at our layer.** The portable LGX bundler includes all transitive dependencies. Bundled `libpcsclite.so.1` can't find the system `pcscd` socket. We had a post-install workaround (delete the file), but that's not a real solution. With keycard-basecamp, the keycard module manages its own PC/SC connectivity.

**3. Go callback threads don't work in logos_host.** This was the hardest lesson from v1.0.0. `KeycardSetSignalEventCallback` fires on a Go goroutine thread that never reaches the Qt event loop inside the plugin host process. We worked around it with RPC polling, but the fundamental problem is that Go shared libraries are a poor fit for the logos_host plugin architecture. Moving keycard to its own module (which can manage its own process lifecycle) eliminates this class of bugs entirely.

**4. Shared keycard identity across the ecosystem.** One card, one PIN, all Logos modules unlocked. Wallet, chat, notes — they all call the same keycard-basecamp module. No re-pairing, no separate PIN entries.

## How the IPC flow works

The QML layer drives the flow with two `logos.callModule` calls:

### 1. Request authorization

```javascript
var result = logos.callModule("keycard", "requestAuth", ["notes_encryption", "notes"])
// Returns: { "authId": "abc123" }
```

The first argument (`"notes_encryption"`) is the domain — keycard-basecamp derives a domain-specific key so different modules get different keys from the same card. The second (`"notes"`) identifies the requesting module.

### 2. Poll for completion

```javascript
var result = logos.callModule("keycard", "checkAuthStatus", [authId])
// Returns: { "status": "complete", "key": "af42...64hex..." }
//      or: { "status": "pending" }
//      or: { "status": "failed", "error": "..." }
```

A QML Timer polls every second. When status is `"complete"`, the hex key is passed to the C++ backend:

```javascript
logos.callModule("notes", "importWithKeycardKey", [keycardDerivedKey, backupPath])
// or for unlock:
logos.callModule("notes", "unlockWithKeycardKey", [keycardDerivedKey])
```

### 3. Backend receives the key directly

```cpp
void NotesBackend::importWithKeycardKey(const QString &hexKey, const QString &backupPath)
{
    QByteArray keyBytes = QByteArray::fromHex(hexKey.toUtf8());
    SecureBuffer masterKey(keyBytes.left(32));
    sodium_memzero(keyBytes.data(), keyBytes.size());

    m_db.saveMeta("key_source", "keycard");
    m_db.saveMeta("account_fingerprint", deriveFingerprintFromKey(masterKey.ref()));
    m_keys.setMasterKey(masterKey.toByteArray());
    // ...
}
```

No `KeycardBridge`. No JSON-RPC. No Go. Just a hex string over Logos IPC.

## What coexists

Both key derivation paths are fully functional:

| | Mnemonic + PIN | Keycard module |
|--|---------------|----------------|
| Key derivation | Local Argon2id | External keycard-basecamp |
| Key storage | Wrapped with PIN key in SQLite | Not stored — re-derived from card each unlock |
| Fingerprint | Full 64-char Ed25519 hex | First 16 hex characters |
| Brute-force protection | PIN attempt counter + lockout | Card's own PIN retry counter |
| Offline access | Yes (with PIN) | No (card must be present) |
| `key_source` meta | `"mnemonic"` | `"keycard"` |

The `key_source` field determines which unlock screen QML shows and which backend method handles the unlock.

## Epic #62 lessons

### Secure channel state persists across sessions

After `authorizeRequest` completes and the session auto-closes, the card's secure channel is stale. The next `openSecureChannel` fails. Fix: retry with a fresh `select()`. This was a keycard-basecamp problem, not ours, but we hit it during integration testing.

### Bridge state names must match QML state checks

`getState()` returned `"AUTHORIZED"` after auth, but QML only checked `"CARD_PRESENT"`, `"READY"`, `"SESSION_ACTIVE"`. Missing a state means the UI gets stuck. Always enumerate all possible states before writing the QML.

### Always check backend response shape before writing QML

`listBackups()` returns a raw array but QML expected `{backups:[...]}`. This is a recurring pattern: inspect the actual response before writing the parser.

### Rewrite large QML files from scratch

Patching 50+ references in 1500-line QML was error-prone. Rewriting from scratch (850 lines) was faster and produced cleaner code. When the change touches more than ~30% of a file, start fresh.

## What was removed

| Artifact | Status |
|----------|--------|
| `KeycardBridge` class (`.h`/`.cpp`) | Removed from source |
| `libkeycard.so` runtime dependency | Removed — keycard-basecamp handles it |
| `libpcsclite.so.1` bundling + workaround | No longer needed |
| Go JSON-RPC (`KeycardCallRPC`, etc.) | No longer used |
| `scripts/build-libkeycard.sh` | Removed |
| `lib/keycard/libkeycard.h` | Removed |

## The takeaway

The right architecture for hardware key support in a plugin ecosystem is a shared module, not an embedded library. We learned this the hard way — through Go callback threads that don't fire, bundled system libraries that can't find their sockets, and 14MB of CGO binary that every module would have had to duplicate.

The migration from embedded `libkeycard.so` to `logos.callModule("keycard", ...)` reduced our plugin's complexity, eliminated an entire class of cross-process bugs, and enabled keycard reuse across the Logos ecosystem. The IPC overhead is negligible — a few JSON strings over a local socket, once per unlock.

Sometimes the best code you write is the code you delete.

---

*Built with keycard-basecamp, libsodium, Qt 6.9, and the wisdom of having debugged Go goroutines inside a Qt plugin host.*
