# Halt — 2026-04-11 (UI modules now working — needs commit + merge)

## Where we stopped

UI modules (notes_ui, keycard-ui) now appear in the Logos Basecamp sidebar after
fixing the stale `package_manager` module. Both branches are ready for final commit
and Senty review.

## Current state

- **logos-notes branch:** `feature/new-appimage-compat` at `a24c1b1`
- **keycard-basecamp branch:** `feature/new-appimage-compat` at `d435226`
- **AppImage:** `~/logos-app/result/logos-basecamp.AppImage` (ce48695+)
- **notes_ui:** visible in sidebar ✓
- **keycard-ui:** should also be visible (same fix applied)
- **notes core module:** loads and runs ✓
- **keycard core module:** loads ✓

## What was fixed

### Fix 1: manifests updated to v0.2.0 (committed)
- `modules/notes/manifest.json` — kept `"main"` map for .so files
- `plugins/notes_ui/manifest.json` — `"main": {}` + `"view": "Main.qml"`
- Same for keycard module and keycard-ui in keycard-basecamp repo
- `metadata.json` — changed `"main": "Main.qml"` → `"view": "Main.qml"`

### Fix 2: stale package_manager module replaced (NOT committed yet)
The user-installed `~/.local/share/Logos/LogosBasecamp/modules/package_manager/` had v0.1.0
which is missing `getInstalledUiPlugins` entirely. Replaced at runtime with AppImage embedded v0.2.0.

This fix is currently just a manual file copy — not committed anywhere. See next steps.

## Root cause (for reference)
Since commit `113b67c` (Mar 27), `MainUIBackend` calls `package_manager.setUserUiPluginsDirectory()`
and `package_manager.getInstalledUiPluginsAsync()`. Old v0.1.0 has neither. New API calls
silently returned nothing — no error, empty sidebar. Confirmed via `nm -D` symbol comparison.

See `docs/skills/appimage-module-versioning.md` for full details.

## Next steps (in order)

1. **Verify notes_ui actually loads** — click it in the sidebar, confirm PIN screen appears
2. **Commit skill doc**: `git add docs/skills/appimage-module-versioning.md docs/retro-log.md && git commit`
3. **The package_manager fix needs to be documented as an install step** — not something we commit,
   but something the install guide documents. Update `docs/skills/sdk-upgrade-guide.md` or create
   a new "Basecamp compatibility guide".
4. **Senty review** — post handoff on both feature/new-appimage-compat branches
5. **Merge both branches** after Senty LGTM
6. **Post Builder Hub** — capability token question (`docs/upstream/discord-builder-hub-draft.md`)

## Remaining known issues

- **Capability deny still unresolved** — storage_module returns "false" for notes plugin.
  Upstream question drafted. Post after compat branches are merged.
- **Stale legacy libs** in notes module dir: `libkeycard.so`, `libpcsclite.so.1` still present
  from before Epic #62. Not breaking but worth cleaning.

## Notes on the package_manager version fix

The manual fix copies from the AppImage mount (only available while AppImage is running):
```bash
MOUNT=$(find /tmp -maxdepth 1 -name ".mount_logos-*" -type d | head -1)
for mod in package_manager capability_module; do
    cp $MOUNT/usr/modules/$mod/*.so ~/.local/share/Logos/LogosBasecamp/modules/$mod/
    cp $MOUNT/usr/modules/$mod/manifest.json ~/.local/share/Logos/LogosBasecamp/modules/$mod/
done
```

Long-term: logos-app should prefer embedded framework modules over user-installed stale copies.
This is an upstream issue to file.
