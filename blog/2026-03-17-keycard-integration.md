# Keycard Integration: Hardware Keys for Encrypted Notes

*March 17, 2026 — v1.0.0 release*

This post is for the builders. If you're working with Status Keycard, `status-keycard-go`, or building Logos App modules that need hardware key management, this is what we learned.

## What we shipped

Immutable Notes v1.0.0 adds Keycard hardware key derivation. Your notes are encrypted with a key that lives on a physical smart card. No card, no notes. The card must be present every time you unlock.

The user experience is intentionally simple: plug in reader, insert card, enter PIN, done. Behind that simplicity is a stack of lessons about Go shared libraries, QML sandboxes, AppImage packaging, and PC/SC daemon wrangling.

## The library: status-keycard-go

We started with `status-keycard-go` — the same Go library that powers Keycard in Status Desktop. It compiles to `libkeycard.so` via CGO:

```bash
cd status-keycard-go/shared
go build -buildmode=c-shared -o libkeycard.so .
```

This gives you a C shared library with a JSON-RPC interface:

```c
char* KeycardInitializeRPC(void);
char* KeycardCallRPC(char* payload);
void  KeycardSetSignalEventCallback(void* cb);
```

The library exposes two APIs:

1. **Session API** — individual RPC calls: `keycard.Start`, `keycard.Authorize`, `keycard.ExportLoginKeys`, `keycard.GetStatus`
2. **Flow API** — composite operations: `KeycardStartFlow(flowType, params)` where `Login` (type 3) does auth + export in one shot

We use the Session API for detection/polling and the Session API for auth + key export. Here's why.

## What worked

### Session API for card detection

The RPC-based polling works reliably inside the Logos App plugin host process (`logos_host`):

```cpp
QJsonObject response = rpcCall("keycard.GetStatus");
// Returns: {"result":{"state":"ready","keycardInfo":{...},"keycardStatus":{...}}}
```

We poll every 500ms from QML via `callModule`, and it correctly tracks reader connection, card insertion, card removal, and PIN verification state. The Go library maintains a background goroutine that monitors the PC/SC daemon and updates its internal state machine.

States we handle: `waitingForReader` → `waitingForCard` → `ready` → `authorized` (and error states: `noPCSC`, `notKeycard`, `emptyKeycard`, `blockedPIN`, `blockedPUK`, `connectionError`).

### Session API for auth + export

After much debugging (see "What didn't work" below), the Session API's `Authorize` + `ExportLoginKeys` works when called correctly:

```cpp
// Authorize
QJsonObject params;
params["pin"] = pin;
QJsonObject authResponse = rpcCall("keycard.Authorize", params);
// {"result":{"authorized":true},"error":null}

// Export encryption key
QJsonObject exportResponse = rpcCall("keycard.ExportLoginKeys");
// {"result":{"keys":{"encryptionPrivateKey":{"privateKey":"af42...","publicKey":"042a..."},...}}}
```

The encryption private key is a 32-byte secp256k1 key at the EIP-1581 encryption derivation path (`m/43'/60'/1581'/1'/0`). We use it with domain separation to derive our AES-256-GCM master key.

### Domain separation

Different apps should derive different keys from the same card. We achieve this with:

```cpp
SHA256(secp256k1_private_key || "logos-notes-encryption") → 256-bit AES key
```

This is deterministic (same card always produces same notes key), domain-separated (a chat app using the same card gets a different key), and produces the correct output size for AES-256-GCM.

### Card identity verification

On unlock, we re-derive the fingerprint from the exported key and compare it against the stored `account_fingerprint`. If someone inserts a different Keycard with a valid PIN, we reject it before any notes are decrypted:

```cpp
QString derivedFp = deriveFingerprintFromKey(masterKey.ref());
QString storedFp = m_db.loadMeta("account_fingerprint", "");
if (!storedFp.isEmpty() && derivedFp != storedFp) {
    setError("Wrong keys. Try different Keycard.");
    return;
}
```

### Auto-lock on card removal

A guard timer polls card state every 2 seconds while notes are open. If the card or reader disappears, the session locks immediately:

```qml
Timer {
    interval: 2000
    running: root.keySource === "keycard" && root.currentScreen === "note"
    onTriggered: {
        var st = pollKeycardState()
        if (st === "waitingForCard" || st === "waitingForReader") {
            lockSession()
            errorMessage = "Keycard removed — session locked"
        }
    }
}
```

## What didn't work

### Go signal callbacks in logos_host

The Go library offers push signals via `KeycardSetSignalEventCallback`. When card state changes, the Go goroutine calls your C function pointer. This works perfectly in a standalone process (we verified with Python tests). But inside `logos_host` — the Logos App plugin host — **the callbacks never fire**.

The Go goroutine runs on its own thread, and the callback fires on that thread. Inside the multi-threaded Qt plugin host, these signals never reach our code. We tried both Session API signals and Flow API signals (`keycard.flow-result`). Neither works.

**Solution**: use RPC polling (`keycard.GetStatus`) instead of push signals. Poll every 500ms. It's slightly less efficient but completely reliable.

### Missing JSON-RPC params field

This one cost hours. The Go JSON-RPC framework returns `{"result":null}` when the `"params"` field is missing from the request, even for methods with no arguments. Our C++ `rpcCall` method was omitting params for no-arg methods:

```json
// Broken — no params field
{"jsonrpc":"2.0","id":"1","method":"keycard.ExportLoginKeys"}

// Working — empty params array
{"jsonrpc":"2.0","id":"1","method":"keycard.ExportLoginKeys","params":[{}]}
```

This caused `ExportLoginKeys` to return null keys after a successful `Authorize`. The authorize call included params (the PIN), so it worked. The export call had no params, so it silently failed.

The fix:

```cpp
// Always include params
QJsonArray paramsArray;
paramsArray.append(params.isEmpty() ? QJsonObject() : params);
request["params"] = paramsArray;
```

### Flow API in logos_host

The Flow API (`KeycardStartFlow(3, ...)` for Login) does auth + export atomically — no mutex issues, no race conditions. It works perfectly in Python. But inside `logos_host`, it relies on signal callbacks to deliver the result, and as noted above, those don't work.

We spent significant time trying to make the Flow API work: `std::atomic<bool>` flags, `QThread::msleep` wait loops, `QCoreApplication::processEvents()`. None of it helped because the Go callback thread simply doesn't reach our plugin process.

**Verdict**: Flow API is the correct design for standalone apps. For Logos App plugins, stick to Session API RPC calls.

### AppImage library sandboxing

Inside the Logos App's AppImage, `logos_host` runs with `LD_LIBRARY_PATH` pointing only to the AppImage's bundled libraries. System libraries like `libpcsclite.so.1` are invisible.

Our `libkeycard.so` links against `libpcsclite.so.1` (the PC/SC client library that talks to the `pcscd` daemon). Inside the AppImage, it can't find it.

**Solution**: bundle `libpcsclite.so.1` in the same directory as our plugin and set `$ORIGIN` RPATH:

```cmake
# Find and install system libpcsclite
find_file(PCSCLITE_SO "libpcsclite.so.1" PATHS /lib/x86_64-linux-gnu ...)
install(FILES "${PCSCLITE_REAL}" DESTINATION "${MODULE_DIR}" RENAME "libpcsclite.so.1")

# Fix RPATH on libkeycard.so
install(CODE "execute_process(COMMAND patchelf --set-rpath $ORIGIN ...)")
```

### CMake IMPORTED libraries embed absolute paths

When you use `add_library(keycard SHARED IMPORTED)` with `IMPORTED_LOCATION`, CMake embeds the full build path as the `NEEDED` entry in the linked binary. Inside the AppImage, that path doesn't exist.

**Solution**: use `link_directories()` + link by name instead:

```cmake
link_directories("${KEYCARD_LIB_DIR}")
target_link_libraries(notes_plugin PRIVATE keycard)
```

This produces `NEEDED: libkeycard.so` (relative name) instead of `NEEDED: /home/user/logos-notes/lib/keycard/libkeycard.so` (absolute path).

### SVG icons blocking mouse events in QML

Loading SVG icons via `Image { source: "file.svg" }` works in the Logos App QML sandbox. But the Image element can intercept mouse events, making buttons behind it unclickable.

**Solution**: always put `MouseArea { z: 10 }` to ensure clicks pass through:

```qml
Image {
    source: "Lock.svg"
    width: 16; height: 16
}
MouseArea {
    z: 10  // Above the Image
    anchors.fill: parent
    onClicked: lockSession()
}
```

### Killing AppImage processes reliably

The Logos AppImage wraps processes via `ld-linux`:

```
/lib64/ld-linux-x86-64.so.2 /tmp/.mount_logos-XXX/usr/bin/.LogosApp.elf
```

So `pkill -f logos` doesn't match them. Two instances can run simultaneously, fighting over the same database and causing data loss.

**Solution**: kill by the actual binary names:

```bash
pkill -9 -f "LogosApp.elf"
pkill -9 -f "logos_host.elf"
```

## Architecture

```
User inserts Keycard
  → PC/SC daemon (pcscd) detects card
    → libkeycard.so (Go, via CGO) monitors via Session API
      → KeycardBridge (C++) wraps JSON-RPC calls
        → NotesBackend calls authorize() + exportKey()
          → Domain separation: SHA256(key || "logos-notes-encryption")
            → 256-bit AES-256-GCM master key
              → Notes encrypted/decrypted
```

No key material touches disk. The master key lives in memory only, wrapped in a `SecureBuffer` (RAII with `sodium_memzero`). When the card is removed, the key is wiped.

## What's next

- **v1.1.0**: Extract `KeycardBridge` into a shared `keycard-module` for the Logos ecosystem. Other apps (wallet, chat) can use the same card without re-pairing.
- **Card initialization wizard**: set up a blank card from within the app (currently requires Keycard Shell or Status Desktop).
- **Migration path**: switch between Keycard and recovery phrase encryption without losing notes.

## Resources

- [status-keycard-go](https://github.com/status-im/keycard-go) — Go library we wrap
- [keycard-tech/status-keycard](https://github.com/keycard-tech/status-keycard) — the Java Card applet source
- [keycard.tech/developers](https://keycard.tech/en/developers/overview) — official Keycard docs
- [keycard-py](https://github.com/mmlado/keycard-py) — Python reference implementation (useful for understanding APDU protocol)
- [logos-notes](https://github.com/xAlisher/logos-notes) — our repo

---

*Built with Keycard, libsodium, Qt 6.9, and late nights debugging Go callbacks that never fire.*
