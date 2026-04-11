# Logos Basecamp — Upstream Findings & Bootcamp Questions

**Context:** Logos Notes + Keycard integration with LogosBasecamp AppImage ce48695
**Date:** 2026-04-11
**Filed by:** Fergie (Claude Code) on behalf of Alisher

---

## Filed Issues

| # | Title | Status |
|---|-------|--------|
| [#141](https://github.com/logos-co/logos-basecamp/issues/141) | Dependency auto-loading broken for user-installed third-party modules (ui_qml) | filed |
| [#142](https://github.com/logos-co/logos-basecamp/issues/142) | ui_qml manifest: 'view' field required but undocumented — 'main' with QML path silently ignored | filed |
| [#136](https://github.com/logos-co/logos-basecamp/issues/136) | UI plugin not loading: basecamp ignores manifest.json 'main' field (related, different reporter) | pre-existing |
| [#105](https://github.com/logos-co/logos-basecamp/issues/105) | Module dependency auto-loading not working for third-party modules (related) | pre-existing |

---

## What We Found (Deep Exploration)

### 1. The `view` field (ce48695 breaking change)
- `ui_qml` now requires `"view": "Main.qml"` + `"main": {}`
- Old format `"main": {"linux-amd64": "Main.qml"}` silently ignored — no error
- `resolveQmlViewPath` reads `meta.value("view")` exclusively
- Fix: 2-line manifest change. Already applied on `feature/new-appimage-compat`.

### 2. Dependency auto-loading (#105 / #141)
- Only AppImage-bundled modules auto-spawn (capability_module, package_manager)
- User-installed modules listed in `dependencies[]` never spawn
- Result: QML loads after 30s timeout, all `callModule()` return `{"error":"Module not connected"}`
- `LogosQmlBridge` has no `loadModule` — cannot trigger from QML
- **This blocks notes_ui from functioning end-to-end**

### 3. Old `type: "ui"` (QWidget IComponent) is deprecated
- Known MDI layout glitches: counter bleeds into tictactoe (#114)
- Being actively removed: #126, #118
- Tictactoe itself shows these glitches in OG AppImage with clean environment
- Do not build new plugins on this path

### 4. New architecture (ce48695)
- `type: "ui_qml"` with optional C++ backend in separate `logos_host` process
- QML loads in-process in basecamp via `LogosQmlBridge`
- C++ backend spawned by `ViewModuleHost` — communicates via socket + Qt Remote Objects replica factory
- Reference: `package_manager_ui` (QML + backend), `counter_qml` (QML-only)
- Our notes_ui = QML-only, correct approach, blocked only by #141

### 5. `LogosQmlBridge` surface (complete)
- Only one QML-callable method: `callModule(module, method, args) → JSON string`
- Returns `{"error":"Module not connected"}` immediately if module not running
- No `loadModule`, no events, no async — synchronous call-response only

### 6. Ghost IPC dirs
- Each AppImage run leaves `logos_*` dirs in `/tmp/` — never cleaned up
- Accumulates to 400+ dirs across sessions, causes IPC noise
- Workaround: `cd /tmp && ls | grep "^logos_" | xargs rm -rf` before each launch

---

## Bootcamp Questions

These are for direct conversation with the Logos team — not for ecosystem devs who haven't gone this deep.

**Architecture**
1. What is the intended path for a third-party `ui_qml` module whose dependencies are also user-installed? Is manual pre-loading the expected UX, or is #105/#141 a bug with a fix coming?
2. Is `ViewModuleHost` + Qt Remote Objects replica factory the canonical IPC pattern for new ui_qml C++ backends? Is there documentation or a reference implementation beyond `package_manager_ui`?
3. Is `LogosQmlBridge.callModule` the only intended QML→module surface? No events/subscriptions planned?

**Roadmap**
4. Timeline for removing `type: "ui"` support? What's the migration path for modules following the old tutorial?
5. Is the `view` field requirement for `ui_qml` documented anywhere? The module-builder tutorial (tutorial-v1) still shows the old format.
6. What is the intended `manifestVersion` for ce48695+ compatible plugins? `"0.2.0"` observed in AppImage-bundled plugins — is that the canonical version?

**Practical**
7. What's the best channel to report AppImage integration bugs during active development — GitHub issues, Discord, Builder Hub?
8. Is there a changelog or migration guide for breaking changes between AppImage releases?

---

## Our State Going Into Bootcamp

| Component | State | Notes |
|-----------|-------|-------|
| notes core (C++ plugin) | ✅ solid | 7/7 tests, full encryption |
| notes_ui manifest | ✅ correct | `type: "ui_qml"`, `view: "Main.qml"` |
| notes_ui QML | ✅ correct | blocked only by #141 |
| AppImage end-to-end | ❌ blocked | upstream #141 (dependency auto-loading) |
| Keycard integration | ✅ correct | blocked same reason |
