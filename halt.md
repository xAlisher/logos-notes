# Halt — 2026-04-11 (new AppImage compat — UI modules not showing)

## Where we stopped

Working on `feature/new-appimage-compat` to make notes and keycard work in the
new Logos Basecamp AppImage (ce48695+). Core modules (notes, keycard) now load
without crash. UI modules (notes_ui, keycard-ui) still don't appear in the sidebar.
Root cause is partially understood — see next steps.

## Current state

- **logos-notes branch:** `feature/new-appimage-compat` at `c1560b2`
- **keycard-basecamp branch:** `feature/new-appimage-compat` at `d435226`
- **Master:** `cfa217a` (SDK upgrade retro, stable)
- **AppImage:** `~/logos-app/result/logos-basecamp.AppImage` (ce48695+, latest)
- **Build:** `build-new/` — builds clean, 63/63 targets
- **Tests:** 7/7 pass (inside nix develop)

## What was fixed

1. **manifest.json** updated to v0.2.0 on both modules and UI plugins:
   - `notes`: keeps `"main": {"linux-amd64": "notes_plugin.so"}` (core type)
   - `notes_ui`: uses `"main": {}` + `"view": "Main.qml"` (ui_qml type)
   - Same pattern for keycard and keycard-ui
2. **metadata.json** updated: `"main": "Main.qml"` → `"view": "Main.qml"` on notes_ui and keycard-ui
3. **Core modules load**: notes and keycard appear in module stats, no crash
4. **New LGX packages built**: `~/Desktop/logos-notes-core.lgx` and `~/Desktop/logos-notes-ui.lgx`

## The remaining problem

UI plugins do NOT appear in the sidebar after LGX install attempt.

### What we know

Since commit `113b67c` (Mar 27, 2026), Basecamp uses `package_manager.getInstalledUiPluginsAsync()`
to populate the sidebar — not a file scan. The package_manager module scans
two directories set by:
- `setEmbeddedUiPluginsDirectory()` — AppImage embedded plugins (counter_qml etc.)
- `setUserUiPluginsDirectory()` — user-installed plugins

**Key unknown:** What exact directory does `setUserUiPluginsDirectory()` point to in the
new AppImage? The research suggests it might be `~/.local/share/logos_host/plugins/`
(not `~/.local/share/Logos/LogosBasecamp/plugins/` where we've been installing).

Also: `package_manager.db` was not found in `module_data/package_manager/63d20fa9b4b8/`
(empty directory). Either the DB lives elsewhere or the package_manager uses directory
scan (not DB) for user plugins.

### What to try next

1. **Check exact user plugins path**: In logos-app source, find what value is passed to
   `setUserUiPluginsDirectory()` in the app startup code. Compare to where we're installing.

2. **Check if counter_qml (user-dir copy) appears in sidebar**:
   `~/.local/share/Logos/LogosBasecamp/plugins/counter_qml/` exists. If it doesn't appear,
   the whole user plugins dir is wrong path.

3. **Try installing to `~/.local/share/logos_host/plugins/`** if that's the real path.

4. **Check the package_manager module source** (in logos-package-manager-module repo or
   embedded in logos-app) for what directory is used.

## Next steps (in order)

1. In logos-app, grep for `setUserUiPluginsDirectory` to find exact path used at runtime
2. Verify by checking if any user-installed plugin shows in sidebar (counter_qml is a good test)
3. Copy notes_ui files to the correct path and retest
4. If still broken: check package_manager module source in logos-app's nix deps
5. Once working: commit + post Senty review + merge both branches

## Context

- **AppImage path confusion**: `~/.local/share/logos_host/` vs `~/.local/share/Logos/LogosBasecamp/`
  — these are two different directories. `logos_host` is used by notes.db and keycard_pairings.json.
  The `module_data/package_manager/63d20fa9b4b8/` dir is empty (no DB found there).
- **Retro intel**: LGX install test on Desktop files was tried but didn't surface notes_ui in sidebar.
  Unclear if the install succeeded silently or if the path is wrong.
- **logos-app source file**: `src/MainUIBackend.cpp` lines 93-127 has `setEmbeddedUiPluginsDirectory`
  and `setUserUiPluginsDirectory` calls — check what paths are passed.
- **Tictactoe PR**: still open at https://github.com/fryorcraken/logos-module-tictactoe/pull/1
- **Capability deny**: storage_module still denies notes. Upstream question drafted at
  `docs/upstream/discord-builder-hub-draft.md`. Post to Builder Hub after UI fix.

## LGX packages on Desktop

- `~/Desktop/logos-notes-core.lgx` — built from `feature/new-appimage-compat` (v0.2.0 manifests)
- `~/Desktop/logos-notes-ui.lgx` — built from same branch
