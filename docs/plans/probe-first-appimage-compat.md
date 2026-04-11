# Plan: Probe-first — understand before fixing

**Status:** Active
**Branch:** `feature/new-appimage-compat`
**Epic:** #79
**Date:** 2026-04-11

---

## User constraints (non-negotiable)

1. **No patched binaries** — only unmodified AppImage modules (capability_module, package_manager, etc.)
2. **Tictactoe = reference architecture** — it's built the right way; probe must mirror it exactly
3. **Master stays usable** — notes on master must remain working; we do not touch master
4. **Working state must be logged first** — document which AppImage + which master commit works before any changes
5. **Write plan before implementing** — this document is that plan

---

## Current state audit

### master branch (do not touch)
- Commit: `9eabef1`
- `plugins/notes_ui/manifest.json`: `type: "ui_qml"`, `manifestVersion: "0.1.0"`, `main: {"linux-amd64": "Main.qml"}`
- `CMakeLists.txt`: no `notes_ui_plugin` target — builds notes_plugin.so (core) only
- All 7 tests pass on master

### feature/new-appimage-compat branch (our working branch — NOT to be merged to master yet)
- `CMakeLists.txt` modified: added `notes_ui_plugin` MODULE target, Qt6::Widgets, Qt6::QuickWidgets
- `plugins/notes_ui/manifest.json` modified: `type: "ui"`, `main: notes_ui_plugin.so`
- `src/ui_plugin/` created: NotesUiPlugin.h, NotesUiPlugin.cpp, ui_plugin_metadata.json
- Patched `capability_module_plugin.so` installed at runtime — **this must be reverted**

### AppImage in use
- `~/logos-app/result/logos-basecamp.AppImage`
- Version/commit: needs to be logged (Phase 0 action)

---

## Phase 0: Log + lock the working state

**Before any new work, document what we know works.**

Sub-issue: #80

1. Log AppImage version (git SHA from logos-app repo)
2. Log master + test status
3. **Revert patched capability_module** — restore AppImage original unmodified binary
4. Write one-paragraph note to `docs/skills/working-baseline.md`:
   - AppImage git SHA
   - master commit SHA
   - Which modules are installed and their versions
   - Observed behavior

---

## Phase 1: Clean tictactoe baseline test

**Verify tictactoe loads cleanly before touching anything else.**

Sub-issue: #81

1. Remove all user modules EXCEPT tictactoe + tictactoe_ui
2. Kill all processes, launch AppImage fresh
3. Tap tictactoe — success criteria:
   - Loads in under 5 seconds
   - No spinner delay
   - Game is playable
4. Tap tictactoe 3 times — confirm no crash
5. Document result

If tictactoe fails here → the issue is in the AppImage IComponent loading pipeline itself, not our modules.

---

## Phase 2: Build probe_ui

**A minimal IComponent C++ plugin built exactly like tictactoe_ui, purpose-built for runtime introspection.**

Sub-issue: #82

### Architecture (mirrors tictactoe exactly)
```
logos-probe/               ← separate directory (not in logos-notes repo)
  src/
    ProbeUiPlugin.h        ← QObject + PluginInterface + IComponent
    ProbeUiPlugin.cpp
    ProbeWidget.h          ← QWidget subclass
    ProbeWidget.cpp        ← widget with buttons + log pane
  probe_ui/
    manifest.json          ← type: "ui", manifestVersion: "0.2.0", deps: []
    probe_ui_metadata.json ← {"name": "probe_ui", "version": "1.0.0"}
  CMakeLists.txt
```

### ProbeWidget buttons
- **Status row**: "LogosAPI: [address or null]" — confirms initLogos was called
- `[Ping capability_module]` → `invokeRemoteMethod("capability_module", "getModuleStatus", [])` → log result
- `[Request notes token]` → `capability_module.requestModule("notes", "")` → log result
- `[Call notes.isInitialized]` → `notes.isInitialized()` → log result
- `[List installed modules]` → `package_manager.getInstalledModules()` → log result
- `[Clear log]`
- **Log pane**: QPlainTextEdit (read-only), timestamped results

### Key constraint
`initLogos(LogosAPI* api)` stores into `PluginInterface::logosAPI` (base field directly). No private shadow field.

### Include paths (from tictactoe reference)
- IComponent.h: `/nix/store/1lz55df1b2sw6gl8f0x1s7hrrvfr2rh3-tictactoe-ui/interfaces/`
- interface.h: `/nix/store/092zxk8qbm9zxqigq1z0a5l901a068cz-logos-liblogos-headers-0.1.0/include/`
- logos_api.h: `/nix/store/047dmhc4gi7yib02i1fbwidxpksqvcc2-logos-cpp-sdk/include/cpp/`
- Link: `Qt6::Core Qt6::Widgets` + `liblogos_sdk.a`

---

## Phase 3: Use probe findings to plan notes_ui fix

**Run through every button in the probe. Decision gate determines next action.**

Sub-issue: #83

| Button | Expected | Means |
|--------|----------|-------|
| Ping capability_module | any JSON | Loading pipeline works |
| Request notes token | non-empty token | capability_module works correctly (unpatched) |
| Request notes token | `""` or `"false"` | capability token issue — file upstream question |
| Call notes.isInitialized | `"true"` or `"false"` | notes logo_host running, IPC works |
| Call notes.isInitialized | error/timeout | notes not started or IPC broken |
| List modules | JSON list | package_manager healthy |

**Decision gate:**

| Probe outcome | Next action for notes_ui |
|---------------|--------------------------|
| All buttons work | 30-40s spinner is cold-start wait. Add loading indicator to notes_ui. |
| capability token = "" | File Builder Hub question. Cannot fix in our code. |
| notes.isInitialized errors | Investigate notes logo_host spawn — check manifest deps field |
| probe itself crashes on load | IComponent contract issue — compare against tictactoe symbol table |

---

## What we are NOT doing
- Not patching capability_module — use only unmodified AppImage binaries
- Not merging feature/new-appimage-compat to master before probe validates
- Not changing master's manifest.json or CMakeLists.txt
- Not using logos-dev-boost or any non-tictactoe-style architecture
- Not building probe inside logos-notes repo (separate directory)

---

## Critical reference files

| File | Role |
|------|------|
| `/nix/store/1lz55df1b2sw6gl8f0x1s7hrrvfr2rh3-tictactoe-ui/` | Gold standard plugin to mirror |
| `~/logos-app/src/PluginLoader.cpp` lines 87-191 | Full loading pipeline |
| `~/logos-app/src/MainUIBackend.cpp` lines 638-655 | loadLegacyUiModule (what ui type triggers) |
| `/nix/store/047dmhc4gi7yib02i1fbwidxpksqvcc2-logos-cpp-sdk/include/cpp/logos_api.h` | LogosAPI surface |
