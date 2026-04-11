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
