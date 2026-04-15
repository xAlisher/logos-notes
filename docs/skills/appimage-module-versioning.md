# AppImage Module Versioning — Lessons Learned (2026-04-11)

## The Problem

User-installed modules in `~/.local/share/Logos/LogosBasecamp/modules/` can become stale
when the AppImage is updated. The AppImage contains newer embedded versions in its
`/usr/modules/` directory (read-only). When `logos_core` loads a module, it prefers the
user-installed version over the embedded one.

## Why It Broke UI Plugin Discovery

Since commit `113b67c` (Mar 27, 2026), `MainUIBackend` calls:
- `package_manager.setUserUiPluginsDirectory(path)` — new API
- `package_manager.getInstalledUiPluginsAsync(callback)` — new API

The old v0.1.0 package_manager only had `setUiPluginsDirectory` (single dir, no user/embedded split)
and NO `getInstalledUiPlugins` method. The new API calls silently did nothing — no error, no log,
empty sidebar.

## How to Detect

Compare symbol tables between embedded and user-installed versions:

```bash
# Embedded (new API):
nm -D /tmp/.mount_logos-*/usr/modules/package_manager/package_manager_plugin.so | grep -i "UserUiPlugins\|getInstalledUi"

# User-installed (check for API presence):
nm -D ~/.local/share/Logos/LogosBasecamp/modules/package_manager/package_manager_plugin.so | grep -i "UserUiPlugins\|getInstalledUi"
```

If the user-installed version is missing these symbols, it must be updated.

## The Fix

Copy framework modules from the embedded AppImage to the user directory:

```bash
MOUNT=$(find /tmp -maxdepth 1 -name ".mount_logos-*" -type d 2>/dev/null | head -1)
for mod in package_manager capability_module; do
    cp -v $MOUNT/usr/modules/$mod/*.so ~/.local/share/Logos/LogosBasecamp/modules/$mod/
    cp -v $MOUNT/usr/modules/$mod/manifest.json ~/.local/share/Logos/LogosBasecamp/modules/$mod/
done
```

Restart the AppImage after copying.

## Affected Modules

Framework modules that the AppImage bundles and can go stale:
- `package_manager` — most critical; v0.2.0 added user/embedded dir split + getInstalledUiPlugins
- `capability_module` — updated alongside package_manager in same AppImage build

## Long-term Fix

The proper fix is to NOT have user-installed copies of framework modules. These should always
be loaded from the AppImage's embedded path. User-installed modules should only be
community/third-party plugins (notes, keycard, tictactoe, etc.).

Filed as investigation: update logos-app to prefer embedded framework modules over user copies.

## Which Fields Are Required for ui_qml Sidebar Visibility

After the package_manager fix, `fetchUiPluginMetadata()` in `MainUIBackend.cpp` (line 752)
requires `"view"` to be non-empty for `ui_qml` plugins. Chain:

```
manifest.json "view": "Main.qml"   ← required
manifest.json "type": "ui_qml"     ← required
manifest.json "name": "notes_ui"   ← required
metadata.json "view": "Main.qml"   ← required by QML loader
```

Do NOT use `"main": "Main.qml"` in metadata.json — must be `"view"`.

## Complete System Architecture (from source research)

### Data flow
```
App startup → subscribeToPackageInstallationEvents()
  → package_manager.setEmbeddedUiPluginsDirectory(<appimage>/usr/plugins/)
  → package_manager.setUserUiPluginsDirectory(~/.local/share/Logos/LogosBasecamp/plugins/)
  → fetchUiPluginMetadata()
    → package_manager.getInstalledUiPluginsAsync(callback)
      → scans both dirs, reads manifest.json per subdir, filters by type
      → returns QVariantList with all manifest fields + "installDir" + "mainFilePath"
    → callback: drops ui_qml where "view" is empty (MainUIBackend.cpp:752)
    → emits uiModulesChanged() → sidebar updates
```

### Manifest field requirements for sidebar visibility

| Field | Required | Notes |
|-------|----------|-------|
| `"type": "ui_qml"` | YES | |
| `"view": "Main.qml"` | YES | Empty → silently dropped at MainUIBackend.cpp:752 |
| `"name"` | YES | |
| `"main": {}` | YES | Must be empty object for QML-only plugins |
| `"manifestVersion": "0.2.0"` | NO | But required by scanner to parse correctly |
| `"hashes"` | NO | Only in AppImage-embedded plugins, not required |

### Why old v0.1.0 package_manager silently failed
The old `package_manager_plugin.so` exported `setUiPluginsDirectory` (single dir).
MainUIBackend (post-113b67c) calls `setUserUiPluginsDirectory` + `setEmbeddedUiPluginsDirectory` (split dirs).
Old module received calls to methods it didn't have → silently ignored → directory never set → empty scan → empty sidebar.
No error, no log.

### Which plugins appear in sidebar vs not

After fix, from user plugins dir:
- notes_ui ✓ (v0.2.0, has "view")
- keycard-ui ✓ (v0.2.0, has "view")
- snake-ui / my-module-ui / auth_showcase-ui ✓ (older format but still visible — package_manager returns them)
- counter_qml (user dir copy) ✗ — v0.1.0, missing "view" field, correctly excluded
- C++ ui plugins (tictactoe_ui, storage_ui, etc.) — excluded by mainFilePath check if .so not found
