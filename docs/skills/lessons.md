# Lessons Learned — logos-notes specific

> Platform-wide lessons live in `~/basecamp-skills/skills/platform-lessons.md` and
> `~/basecamp-skills/skills/qml-patterns.md`. This file contains only lessons specific
> to the Notes module internals (crypto, DB schema, UI flow, account management).

---

### 1. NotesPlugin is the only surface QML can see
Every method added to NotesBackend that QML needs must also be explicitly added to NotesPlugin as Q_INVOKABLE. QML callModule calls silently fail (no error, empty response) when the method doesn't exist on the plugin.

### 2. logos.callModule returns JSON for notes methods
`loadNote(id)` returns note text wrapped in JSON. QML must parse the response before assigning to `editor.text`. On error returns `{"error":"..."}` — guard against this. In the `ui_qml` path responses are double-wrapped; use `callModuleParse()` not `JSON.parse()` directly (see basecamp-skills qml-patterns.md).

### 3. Screen state must survive Qt Loader destruction
Qt's Loader destroys the previous screen when switching. Data that needs to survive a screen transition must be passed as arguments to C++ backend methods, not held in QML state. Pass backup path to `importMnemonic()` on the C++ side before any screen switch.

### 4. Restore rollback
`setInitialized()` must be called after successful restore, not before. Failed restore must roll back completely — wipe DB, return to import screen. On any failure, call `resetAndWipe()`.

### 5. Account fingerprint must be deterministic
Fingerprint derived from master key + random salt = unstable (changes per device/import). Derive from mnemonic directly with no salt: SHA-256(normalized_mnemonic) → Ed25519 seed → public key. No salt involved.

### 6. Mnemonic normalization must be shared
BIP39 validation normalizes (NFKD, lowercase, trim) but key derivation was using raw string. Same phrase typed slightly differently = different key. Single shared `normalizeMnemonic()` function, called before every crypto operation.

### 11. Cipher regression from XChaCha20 fallback
Adding a "simple" AES fallback opened cipher persistence, migration, and portability bugs more complex than the problem they solved. Decision: fail-fast on AES-NI unavailability. Net -65 lines. Prefer fail-fast over complex fallback logic for edge cases affecting <0.1% of hardware.

### 14. QStandardPaths::setTestModeEnabled for backend tests
Calling `QStandardPaths::setTestModeEnabled(true)` redirects `AppDataLocation` to a test-specific path. This allows instantiating `NotesBackend` directly in tests without touching the real DB. Combined with `wipeTestData()` between tests for clean state.

### 15. SQLite connections survive file permission changes
Attempted to force per-note restore failures by making the DB read-only mid-import. SQLite's cached connection continued writing. Forcing write failures in import accounting requires mock injection, not filesystem tricks.

### 16. Screen name is "note" not "notes"
`NotesBackend::importMnemonic()` and `unlockWithPin()` call `setScreen("note")`. Tests must compare against `"note"`, not `"notes"`.

### 23. JSON-RPC null error: check isNull, not contains *(historical — libkeycard removed)*
Go JSON-RPC responses include `"error": null` on success. `QJsonObject::contains("error")` returns true for null values. Must check `response.value("error").isNull()` instead.

### 25. Go signal callbacks don't cross logos_host IPC boundaries *(historical — libkeycard removed)*
Go goroutine-based callbacks fire on Go threads. In the logos_host plugin architecture, these don't reliably reach the Qt event loop. Use active RPC polling instead of relying on push signals.

### 28. Go JSON-RPC requires "params" field even for no-arg methods *(historical — libkeycard removed)*
`KeycardCallRPC` returns `{"result":null}` when the `"params"` field is omitted. Always include `"params":[{}]` even for methods with empty args.

### 29. Go callbacks (signals) don't work in logos_host *(historical — libkeycard removed)*
Neither Session API signals nor Flow API signals fire reliably inside the logos_host process.

### 30. Keycard accounts don't store wrapped keys
Mnemonic accounts wrap the master key with a PIN-derived key and store it in `wrapped_key` table. Keycard accounts derive the key from the card on every unlock — no wrapped key stored. The `key_source` meta field ("keycard" or "mnemonic") determines which unlock flow to use.

### 36. Bundled libpcsclite breaks pcscd socket connection *(historical — libkeycard removed)*
The portable bundler includes all transitive dependencies, including `libpcsclite.so.1`. The bundled version cannot connect to the system `pcscd` daemon socket. Since keycard support moved to the external keycard-basecamp module, notes no longer bundles either library.
