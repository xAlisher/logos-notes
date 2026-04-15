# Working Baseline — 2026-04-11

## AppImage
- **Repo:** `~/logos-app`
- **Git SHA:** `ce48695` (update webview app flake)
- **Binary:** `~/logos-app/result/logos-basecamp.AppImage`
- **Extraction path:** `/tmp/appimage_extracted_f71ebf7b2776970edc4d26b6ca1722f2/`

## logos-notes master
- **Commit:** `cfa217a` (docs: SDK upgrade retro + upgrade guide for Logos modules)
- **Tests:** 7/7 passing (`build-new/`)

## Framework modules (AppImage-bundled — NO user overrides)

These are loaded directly from the AppImage. User override directories deleted.

| Module | Version | manifestVersion | Source |
|--------|---------|-----------------|--------|
| `capability_module` | 1.0.0 | 0.2.0 | AppImage bundled (OG) |
| `package_manager` | 1.0.0 | 0.2.0 | AppImage bundled (OG) |

User override dirs removed:
- `~/.local/share/Logos/LogosBasecamp/modules/capability_module/` — deleted (was our patched binary)
- `~/.local/share/Logos/LogosBasecamp/modules/package_manager/` — deleted (was missing libsodium.so.26)

## User-installed modules (`~/.local/share/Logos/LogosBasecamp/modules/`)

| Module | Notes |
|--------|-------|
| `auth_showcase` | framework sample |
| `high_score` | framework sample |
| `keycard` | keycard-basecamp core |
| `mymodule` | dev sample |
| `notes` | logos-notes core (v1.0.0, manifestVersion 0.2.0) |
| `storage_module` | framework module |
| `tictactoe` | reference plugin (gold standard) |

## User-installed plugins (`~/.local/share/Logos/LogosBasecamp/plugins/`)

| Plugin | Notes |
|--------|-------|
| `auth_showcase-ui` | framework sample |
| `counter` | framework sample |
| `counter_qml` | framework sample |
| `keycard-ui` | keycard-basecamp UI |
| `main_ui` | framework |
| `my-module-ui` | dev sample |
| `notes_ui` | logos-notes UI (type: "ui", notes_ui_plugin.so — on feature branch) |
| `package_manager_ui` | framework |
| `snake-ui` | framework sample |
| `storage_ui` | framework |
| `tictactoe_ui` | reference plugin (gold standard) |
| `webview_app` | framework |

## Phase 0 revert rationale
- **capability_module:** our patched binary had a private `logosAPI` field shadowing
  `PluginInterface::logosAPI`. Deleted so AppImage OG is used.
- **package_manager:** user copy was missing `libsodium.so.26` vs AppImage. Deleted so
  AppImage OG (complete) is used.
- Build method going forward: tictactoe pattern exactly (Logos team's reference architecture).
