# Halt — 2026-04-10 (SDK upgrade verified, ready for Phase 2 rebase)

## Where we stopped

SDK upgraded to latest (Apr 9). Notes plugin builds clean, all 7 tests
pass, manual Basecamp verification green (notes load, keycard auth works,
33 notes intact). Auto-backup can't be tested yet because Phase 2 code
is on a separate branch. Next: rebase Phase 2 onto this SDK upgrade and
re-test the capability-token path.

## Current state

- **Branch:** `feature/sdk-upgrade` at `c3a1f79`
- **Master:** `fbf485f` (untouched, Phase 1 stable)
- **feature/v2-autobackup:** `61f2d89` (Phase 2 + #78, old SDK, reference)
- **Build:** `build-new/` — must run inside `nix develop` shell
- **Tests:** 7/7 pass (inside nix develop)
- **Basecamp:** notes green with new SDK plugin

## SDK versions

| What | Rev | Date |
|------|-----|------|
| Old (master) | `95f763b48d74` | 2026-02-25 |
| Tictactoe's | `4197ee183041` | 2026-03-23 |
| **New (this branch)** | `8b1cfadf090f` | **2026-04-09** |

New SDK adds: `logos_object.h`, `logos_registry.h`, `logos_transport.h`,
`plugin_registry.h`, `logos_instance.h`, `logos_types.h`, and more.
Key API changes: `requestObject` returns `LogosObject*` not `QObject*`,
`onEvent` is now 3-arg, new `invokeRemoteMethodAsync`.

## Next steps (in order)

1. Rebase or cherry-pick Phase 2 auto-backup code onto `feature/sdk-upgrade`
2. Update `LogosStorageTransport` for new SDK API (`LogosObject*`, 3-arg `onEvent`)
3. Build and verify tests pass
4. Install to Basecamp, load storage_module (click Storage UI)
5. Save a note, wait 30s, check debug.log for capability-token probe results
6. **If the new SDK resolves the deny:** end-to-end upload works, merge the combined branch
7. **If it doesn't:** the deny is fundamental, file upstream question, replan scope
8. Either way: post-merge protocol (retro, skills, PROJECT_KNOWLEDGE)

## Build note

Tests must run inside `nix develop` shell — the new SDK pulls in
`libzstd.so.1` and other libs not in the system library path:

```bash
export PATH="/nix/var/nix/profiles/default/bin:/usr/bin:/bin:$PATH"
cd ~/logos-notes
nix develop -c bash -c "cd build-new && ctest --output-on-failure"
```

## Branches for reference

| Branch | Commit | Purpose |
|--------|--------|---------|
| `master` | `fbf485f` | Phase 1 stable, old SDK |
| `feature/sdk-upgrade` | `c3a1f79` | New SDK, notes verified green |
| `feature/v2-autobackup` | `61f2d89` | Phase 2 + #78, old SDK, DO NOT merge |

## Context

- Tictactoe module PR submitted: https://github.com/fryorcraken/logos-module-tictactoe/pull/1
- Tictactoe uses generated SDK (`LogosModules`) for IPC — see `docs/skills/generated-sdk-ipc-pattern.md`
- Discord message to @fryorcraken drafted at `docs/upstream/discord-fryorcraken-draft.md`
- Upstream issue drafted at `docs/upstream/issue-draft-capability-tokens-for-core-plugins.md`
- Senty needs status update
- tmux: use native `tmux send-keys -t %2`, not tmux-bridge
