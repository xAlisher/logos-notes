# Halt — 2026-04-11 (investigation complete, blocked on upstream, bootcamp prep)

## Where we stopped

Deep investigation of AppImage ce48695 compatibility complete. Root causes found,
upstream issues filed, findings documented. Waiting on Pavel (vpavlin) response
re: bootcamp and whether core devs (Dario, Helium) will be present.

## Current state

- **Branch:** `feature/new-appimage-compat`
- **Last commit:** `97c70a2` — docs: upstream findings + bootcamp questions
- **Build:** 7/7 tests passing on master
- **AppImage status:** notes_ui loads (30s spinner), buttons non-functional — blocked by upstream #141

## What works

| Component | State |
|-----------|-------|
| notes core (notes_plugin.so) | ✅ solid — 7/7 tests, full encryption |
| notes_ui manifest | ✅ correct — `type: "ui_qml"`, `view: "Main.qml"` |
| notes_ui QML | ✅ correct — blocked only by upstream |
| AppImage end-to-end | ❌ blocked — upstream #141 (dependency auto-loading) |

## What's installed in AppImage user dirs

- `modules/notes/` — notes_plugin.so + manifest + variant
- `modules/keycard/` — keycard_plugin.so + manifest + variant
- `modules/tictactoe/` — reference (kept)
- `plugins/notes_ui/` — Main.qml + manifest (ui_qml) + variant
- `plugins/tictactoe_ui/` — reference (kept)
- Framework modules (capability_module, package_manager) — deleted user overrides, AppImage OG used

## Upstream issues filed

- logos-basecamp #141 — dependency auto-loading broken for user-installed ui_qml modules
- logos-basecamp #142 — `view` field required but undocumented
- Full findings: `docs/upstream/logos-basecamp-findings.md`
- 8 bootcamp questions logged in same file

## Next steps (in order)

1. **Wait for Pavel's response** — determines bootcamp strategy (core devs present? which path?)
2. **If core devs present at bootcamp:** bring questions from `docs/upstream/logos-basecamp-findings.md` directly
3. **If ecosystem devs only:** focus on notes core story, collect contacts, skip AppImage demo
4. **Park feature/new-appimage-compat** — it's correct, just waiting on upstream fix to #141
5. **Do NOT build probe-basecamp** until #141 is resolved — probing a broken dependency path gives bad data

## Blockers

- Upstream logos-basecamp #141 — nothing we can do until Logos team fixes dependency auto-loading
- Pavel's response — determines bootcamp strategy

## Context that's hard to re-derive

- The 30s spinner is the AppImage's dependency loading timeout, NOT LogosQmlBridge blocking
- `LogosQmlBridge` has only one method: `callModule` — returns immediately with error if module not connected
- Ghost IPC dirs in `/tmp/` accumulate across sessions — run `cd /tmp && ls | grep "^logos_" | xargs rm -rf` before every launch
- `type: "ui"` (QWidget IComponent, tictactoe pattern) is deprecated — do not build on it
- New reference for ui plugins: `counter_qml` (QML-only) and `package_manager_ui` (QML + C++ backend)
- `package_manager_ui` has a `_replica_factory.so` for Qt Remote Objects — that's the IPC mechanism for C++ backends
