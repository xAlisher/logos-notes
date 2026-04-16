# Lessons Learned

> Grep this file when you hit a problem. Each lesson was learned the hard way.

---

### 1. NotesPlugin is the only surface QML can see
Every method added to NotesBackend that QML needs must also be explicitly added to NotesPlugin as Q_INVOKABLE. QML callModule calls silently fail (no error, empty response) when the method doesn't exist on the plugin.

### 2. logos.callModule returns JSON, not raw text
`loadNote(id)` returns note text wrapped in JSON. QML must parse the response before assigning to `editor.text`. On error returns `{"error":"..."}` — guard against this. See also lesson #40 — in the `ui_qml` path responses are double-wrapped; use `callModuleParse()` not `JSON.parse()` directly.

### 3. Screen state must survive Qt Loader destruction
Qt's Loader destroys the previous screen when switching. Data that needs to survive a screen transition must be passed as arguments to C++ backend methods, not held in QML state. Pass backup path to `importMnemonic()` on the C++ side before any screen switch.

### 4. Restore rollback
`setInitialized()` must be called after successful restore, not before. Failed restore must roll back completely — wipe DB, return to import screen. On any failure, call `resetAndWipe()`.

### 5. Account fingerprint must be deterministic
Fingerprint derived from master key + random salt = unstable (changes per device/import). Derive from mnemonic directly with no salt: SHA-256(normalized_mnemonic) → Ed25519 seed → public key. No salt involved.

### 6. Mnemonic normalization must be shared
BIP39 validation normalizes (NFKD, lowercase, trim) but key derivation was using raw string. Same phrase typed slightly differently = different key. Single shared `normalizeMnemonic()` function, called before every crypto operation.

### 7. Logos Basecamp testing requires AppImage build
`nix build '.#app'` (local build) expects .lgx packages, not raw .so files. `cmake --install` copies raw files which only work with portable/AppImage builds.

### 8. Kill ALL Logos processes before relaunching
logos_host child processes survive parent LogosBasecamp being killed. They hold stale .so files and block new module loads: `pkill -9 -f "logos_host.elf"; pkill -9 -f "LogosBasecamp.elf"`.

### 9. QML sandbox restrictions in ui_qml plugins
- No access to Logos.Theme or Logos.Controls imports
- No native file dialogs (FileDialog blocked)
- No file I/O from QML
Workaround: hardcode hex palette values, file I/O via C++ plugin, fixed well-known paths.

### 10. plugin_metadata.json must be fully populated
Empty `{}` metadata means the shell never registers the plugin. Must match manifest.json content with correct IID.

### 11. Cipher regression from XChaCha20 fallback
Adding a "simple" AES fallback opened cipher persistence, migration, and portability bugs more complex than the problem they solved. Decision: fail-fast on AES-NI unavailability. Net -65 lines. Prefer fail-fast over complex fallback logic for edge cases affecting <0.1% of hardware.

### 12. Python brace counting is unreliable for QML validation
QML uses `{}` in JavaScript blocks differently from QML object declarations. Python counting gives wrong results. Always use `qmllint` as the authoritative QML syntax checker.

### 13. CTest must be run from build/, not repo root
Running `ctest` from repo root reports `No tests were found!!!`. CTest registers 4 tests: `test_multi_note`, `test_security`, `test_backup`, `test_account`. These are QtTest binaries with multiple internal cases — CTest will not report the per-case count.

### 14. QStandardPaths::setTestModeEnabled for backend tests
Calling `QStandardPaths::setTestModeEnabled(true)` redirects `AppDataLocation` to a test-specific path. This allows instantiating `NotesBackend` directly in tests without touching the real DB. Combined with `wipeTestData()` between tests for clean state. Established in test_backup.cpp, reused in test_account.cpp.

### 15. SQLite connections survive file permission changes
Attempted to force per-note restore failures by making the DB read-only mid-import. SQLite's cached connection continued writing. Forcing write failures in import accounting requires mock injection, not filesystem tricks.

### 16. Screen name is "note" not "notes"
`NotesBackend::importMnemonic()` and `unlockWithPin()` call `setScreen("note")`. Tests must compare against `"note"`, not `"notes"`.

### 17. nix-bundle-lgx reads metadata.json from drv.src, not lib/ output
The bundler reads `metadata.json` from `drv.src + "/metadata.json"` at Nix eval time, not from the built `lib/` directory. For a mono-repo with multiple packages, put core `metadata.json` at repo root and point UI package `src` to `./plugins/notes_ui/` which has its own `metadata.json`.

### 18. Follow nixpkgs from logos-cpp-sdk for Qt compatibility
The Logos ecosystem pins Qt via `nixpkgs.follows = "logos-cpp-sdk/nixpkgs"`. Our flake must follow the same chain. Mixing nixpkgs versions causes Qt ABI mismatches at runtime.

### 19. initLogos must NOT use override
`initLogos(LogosAPI*)` is called reflectively via `QMetaObject::invokeMethod`, not through virtual dispatch. Using `override` may cause it to not be found. LogosAPI pointer must be stored in the global `logosAPI` variable from `PluginInterface`, not in a class member.

### 20. logos-module-builder simplifies builds
The official `logos-module-builder` flake provides `mkLogosModule` + `module.yaml` — reduces ~300 lines of CMake+Nix to ~70 lines declarative config. Uses `logos_module()` CMake function. Worth migrating to for v1.1.0 (shared keycard-module). Scaffold with `nix flake init -t github:logos-co/logos-module-builder`.

### 21. LogosResult for structured returns
SDK provides `LogosResult` type with `success`, `getString()`, `getInt()`, `getMap()`, `getError()` — cleaner than our raw JSON string approach. Consider adopting for new methods.

### 22. logos-cpp-generator for typed inter-module calls
Auto-generates typed C++ wrappers from compiled modules. Instead of raw `invokeRemoteMethod("module", "method", args)`, get compile-time checked `logos->module.method(args)`. Important for v1.1.0 when keycard-module talks to notes.

### 23. JSON-RPC null error: check isNull, not contains *(historical — libkeycard removed)*
Go JSON-RPC responses include `"error": null` on success. `QJsonObject::contains("error")` returns true for null values. Must check `response.value("error").isNull()` instead. *(Applied to removed libkeycard Go JSON-RPC bridge. General principle still valid for any JSON-RPC work.)*

### 24. AppImage sandbox hides system libraries from plugins
Plugins loaded by `logos_host` inside the AppImage can't find system `.so` files. `LD_LIBRARY_PATH` is set to AppImage paths only. Bundle all transitive dependencies (e.g. `libpcsclite.so.1`) in the module directory and use `$ORIGIN` RPATH.

### 25. Go signal callbacks don't cross logos_host IPC boundaries *(historical — libkeycard removed)*
Go goroutine-based callbacks (like `KeycardSetSignalEventCallback`) fire on Go threads. In the logos_host plugin architecture, these don't reliably reach the Qt event loop. Use active RPC polling instead of relying on push signals. *(Applied to removed libkeycard Go bridge. General principle still valid for any Go shared library in logos_host.)*

### 26. CMake IMPORTED libraries embed full paths in NEEDED
`add_library(IMPORTED)` with `IMPORTED_LOCATION` embeds the absolute build path as the `NEEDED` entry in the linked binary. Use `link_directories()` + link by name instead, so the binary gets a relative `NEEDED` entry that resolves via RPATH.

### 27. install(CODE) must honor DESTDIR for staged builds
`install(CODE)` blocks run post-install scripts. When referencing installed file paths, prefix with `$ENV{DESTDIR}` so staged installs (`DESTDIR=/tmp/staged cmake --install`) work correctly.

### 28. Go JSON-RPC requires "params" field even for no-arg methods *(historical — libkeycard removed)*
`KeycardCallRPC` returns `{"result":null}` when the `"params"` field is omitted from the request JSON. Always include `"params":[{}]` even for methods with empty args (`*struct{}`). *(Applied to removed libkeycard Go bridge.)*

### 29. Go callbacks (signals) don't work in logos_host *(historical — libkeycard removed)*
Neither Session API signals (`KeycardSetSignalEventCallback`) nor Flow API signals (`keycard.flow-result`) fire reliably inside the logos_host process. The Go goroutine thread can't reach the plugin. *(Applied to removed libkeycard Go bridge. See #25 for the general principle.)*

### 30. Keycard accounts don't store wrapped keys
Mnemonic accounts wrap the master key with a PIN-derived key and store it in `wrapped_key` table. Keycard accounts derive the key from the card on every unlock — no wrapped key stored. The `key_source` meta field ("keycard" or "mnemonic") determines which unlock flow to use.

### 31. AppImage wraps processes via ld-linux — pkill must match .elf names
`pkill -9 -f logos` doesn't work because AppImage runs binaries as `/lib64/ld-linux-x86-64.so.2 /tmp/.mount_logos-XXX/usr/bin/.LogosBasecamp.elf`. Must use `pkill -9 -f "LogosBasecamp.elf"; pkill -9 -f "logos_host.elf"` to kill reliably. Two instances fighting over the same DB causes data loss.

### 32. SVG Image elements in QML sandbox need z-order for MouseArea
Loading SVG icons via `Image { source: "file.svg" }` works in the Logos App QML sandbox, but the Image can block mouse events. Always put `MouseArea { z: 10 }` to ensure clicks pass through. Also set `sourceSize: Qt.size(w, h)` for proper SVG rendering.

### 33. Logos Basecamp loads old module versions from backup directories
During development, backup directories like `notes.bak`, `notes.old`, etc. can persist in `~/.local/share/Logos/LogosBasecamp{Dev}/modules/`. The shell may load these stale versions instead of the current build from `notes/`, causing confusing failures (e.g. Keycard detection working in tests but not in the app). **Solution**: CMake install target now removes all `notes.*` directories before installing the current build. This prevents version conflicts and ensures only one module version exists at a time.

### 34. install(CODE) blocks must honor DESTDIR for staged installs
Custom `install(CODE)` blocks that manipulate filesystem paths must prefix those paths with `$ENV{DESTDIR}` to support staged/packaged installs. Example: `set(_path "\$ENV{DESTDIR}${INSTALL_DIR}/file")`. Without this, `DESTDIR=/tmp/stage cmake --install` would still operate on the live system paths instead of the staged tree. This is the same pattern required for post-install scripts like `patchelf`. Caught by Senty in #47 review.

### 35. nix-bundle-lgx platform naming: default vs portable
The default bundler (`nix bundle --bundler github:logos-co/nix-bundle-lgx .#lib`) generates `linux-amd64-dev` variant names. The Logos App Package Manager expects `linux-amd64` without the `-dev` suffix. Use the portable bundler (`#portable`) for correct platform recognition: `nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib`. This bundles all dependencies (including system libraries) for true portability but see lesson #36 for caveats.

### 36. Bundled libpcsclite breaks pcscd socket connection *(historical — libkeycard removed)*
The portable bundler includes all transitive dependencies, including `libpcsclite.so.1` for smart card support. However, the bundled version cannot connect to the system `pcscd` daemon socket (looks for socket in wrong location). *(This was caused by bundling libkeycard.so which depended on libpcsclite. Since keycard support moved to the external keycard-basecamp module, notes no longer bundles either library. General principle still valid: any library that talks to system services via local sockets should use the system version, not a bundled copy.)*

### 37. Logos App renamed to Logos Basecamp (March 2026)
The `logos-co/logos-app` repository was renamed to "Logos Basecamp" (commit 17ef99c). Module paths changed:
- **Portable builds** (AppImage/LGX): `~/.local/share/Logos/LogosBasecamp/{modules,plugins}/`
- **Dev builds** (cmake install): `~/.local/share/Logos/LogosBasecampDev/{modules,plugins}/`
The app now discriminates between dev and portable package variants at build time (`LOGOS_PORTABLE_BUILD` flag). Binary names changed: `LogosApp.elf` → `LogosBasecamp.elf`, `logos-app.AppImage` → `logos-basecamp.AppImage`. Update all install paths and documentation.

### 39. Cleanup commits require filesystem verification, not just diff reading (#61)
When writing cleanup commits that claim "artifacts removed", verify with `ls` / `find` from the project root that files are actually gone. Running from the wrong directory (e.g., `build/`) gives false "not found" results. Also: removing a script/command requires updating every workflow doc and help string that references it in the same commit — otherwise the cleanup itself is a regression.

### 38. QML properties persist across visibility changes causing race conditions (#56)
When a QML component becomes invisible (e.g., module tab closed), properties like `TextEdit.text` retain their values. When the component becomes visible again, those stale values persist while IDs are reset, creating a mismatch window. **Root cause**: `editor.text` contained content from note A, but `activeNoteId` was reset to -1 then changed to note B during auto-selection. If auto-save triggered in this window, it saved wrong content to wrong note. **Solution**: (1) Clear `editor.text = ""` in `onVisibleChanged` to remove stale content. (2) Remove automatic note selection after `refreshList()` — require manual user click. (3) Track `lastLoadedContent` and only save if `editor.text !== lastLoadedContent` (prevents saving unchanged data). **Result**: Eliminated race condition. Lock/unlock worked fine because it's a fast transition; module reopen had a longer window for the race. Reduced auto-save interval from 1000ms to 200ms (feels instant). Added empty state UX when no note selected.

### 40. logos.callModule double-wraps responses in the pure ui_qml path
After switching from `type: "ui"` (IComponent) to `type: "ui_qml"` (commit 48c1900), `logos.callModule` adds an extra JSON string layer around the C++ return. `JSON.parse` alone returns a string not an object — causes phantom entries, silent failures. Always use `callModuleParse()`. Full rule + exceptions in `docs/skills/architecture.md` — QML Bridge section. (Issues #92, #93, #94 — fixed 2026-04-16.)

### 41. Static libstorage.a conflicts with AppImage's libstorage.so — two Nim runtimes crash
A plugin that statically links `libstorage.a` cannot coexist with the AppImage's bundled `storage_module_plugin.so` (which loads `libstorage.so` dynamically). Both try to initialize the Nim runtime, causing a conflict that silently prevents the plugin from loading — no error in log, module just doesn't appear in the sidebar. **Fix**: a thin orchestrator plugin must NOT embed the storage node. Use `logosAPI->getClient("storage_module")` IPC instead of LibStorageTransport. Stash is an orchestration layer, not a storage node.

### 42. Manifest format v0.2.0 required — old `entry` field causes silent load failure
Logos Basecamp (AppImage ≥ d8cfc1b-144) requires `manifestVersion: "0.2.0"` and a `"main"` platform-dispatch map (`{"linux-amd64": "foo.so", ...}`). The old format (`"entry": "foo.so"`, no `manifestVersion`) causes the module to be silently ignored — no error, module simply doesn't appear. Always copy the notes manifest as the canonical reference.

### 43. `pkill` exits 1 when no process matches — never chain with `&&`
`pkill -9 -f "pattern"` returns exit code 1 when nothing matches. Chaining kills with `&&` aborts the chain at the first dead process, leaving remaining processes alive. Always separate kill commands with `;` and append `2>/dev/null; true` for guaranteed zero exit.

### 44. `callModuleParse` must fall back to string on failed second JSON.parse
The double-parse helper breaks on plain-string returns (e.g. `getStatus()` → `"offline"`). After Basecamp wraps: `'"offline"'`. First parse → `"offline"` (string). Second parse → `SyntaxError` → returns `null` → all refreshes silently fail. Fix: wrap the inner `JSON.parse` in its own try/catch and return the string on failure. `try { return JSON.parse(tmp) } catch(e) { return tmp }`.

### 45. CMake `install(CODE)` escaping: `\${var}` defers, `\\${var}` evaluates at configure time
In `install(CODE "...")` blocks, `\${var}` defers expansion to install time (correct). `\\${var}` collapses to `\` + configure-time expansion (empty) → `\)` in cmake_install.cmake → parse error. Use notes CMakeLists.txt as reference for all install block escaping.

### 39. Cleanup claims must be verified against both docs and filesystem
For housekeeping changes, do not trust a narrow diff or memory like "those artifacts were already deleted." A cleanup is only complete when three checks agree: (1) the actual files are gone from the repo, (2) repo-wide search no longer finds the removed command/path in active workflow docs or shell help, and (3) replacement instructions are updated in the same commit. This caught issue #61 where `package-lgx` was removed from the flake surface but still documented in `CLAUDE.md` and shell help, and where docs claimed `lib/keycard/` artifacts were gone while the files still existed.
