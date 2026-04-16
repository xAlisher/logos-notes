# Retro Log

Post-merge retrospectives per `~/fieldcraft/protocols/wins-and-fails.md`.

---

## #75 Online IPFS pinning + notes Stash integration (2026-04-16)

### Fails

- [project] **`background: null` on `QtQuick.TextEdit` silent QML load failure.** Added `background: null` to a `TextEdit` delegate (not `TextField`). Invalid property — `QtQuick.TextEdit` has no `background`. QML parser didn't error; runtime silently failed to instantiate the component. Notes module showed icon in sidebar, clicking it did nothing. Root cause: confused `QtQuick.TextEdit` with `QtQuick.Controls.TextField`/`TextArea` which DO have `background`. Fix: remove the property. Rule: never use `background` on plain `TextEdit`.

- [project] **`variant` file absent from source tree broke LogosApp module loading.** Module showed sidebar icon (discovery OK) but clicking opened nothing. Found `variant` was in LogosBasecamp (stale copy from old install) but missing from LogosApp and from source. Root cause: `variant` was never committed to source, so cmake never installed it to LogosApp. Plugin-discovery.md said "unconfirmed" — now confirmed: `variant` is required for both LogosApp and LogosBasecamp. Fix: committed `variant` to source.

- [project] **Clipboard `TextEdit` inside nested Rectangle scope failed.** Placed `clipboardHelper` inside `activityLog` Rectangle (child scope). `copy()` silently did nothing. Root cause: unknown (scope, focus, or render-tree issue with nested item). The working pattern — `fpHelper` at `noteScreen` Item root, called directly — was already proven. Should have used it from the start instead of creating a new helper inside the Rectangle.

- [project] **Token masking re-save wipes stored token.** `getPinningConfig` returns `token: "***"`. UI set field to `""`. User opened panel without retyping token, clicked Save → `setPinningConfig` received empty token → overwrote stored JWT with `""`. Root cause: masking logic didn't account for re-save without re-entry. Fix: C++ side skips empty token; UI shows `"Token saved — leave blank to keep"` placeholder.

- [project] **`uploadViaIpfs` never wrote to activity log.** Stash UI showed empty log after successful backup. `getLog()` only returns entries appended by `StashBackend.appendLog()`, which is called only from backend-internal ops. Plugin-side `uploadViaIpfs` bypasses the backend entirely. Root cause: assumed direct method calls auto-log. They don't. Fix: made `appendLog` public, called explicitly after pin success/failure.

- [project] **Pinata v3 scoped JWT rejected by v2 `/pinning/pinFileToIPFS` endpoint.** 401 "No Authentication method provided". Switched to v2 endpoint trying to get public IPFS pins. Root cause: v3 scoped JWT is a different auth system from v2 API keys. v3 JWT only works with v3 endpoints. Fix: revert to `/v3/files`; add `network: "public"` form field for public IPFS pinning.

- [project] **CIDv0 vs CIDv1 mismatch caused false "CID mismatch" error.** Local `ipfs --offline add` returns CIDv0 (`Qm...`), Pinata `/v3/files` returns CIDv1 (`bafk...`). Same content, different multihash encoding. Equality check `localCid == remoteCid` always failed. Root cause: assumed CID is a fixed canonical string. Fix: trust remote CID as canonical; drop equality check.

- [process] **Two-app-instance problem recurred.** Kill command failed silently (no processes found, exit 1), pipeline continued, launched new instance on top of old one. Happened three times. Root cause: `kill -9 $(...)` exits non-zero when subcommand returns nothing; didn't verify kill before launch. Fix: split kill and launch into separate steps with explicit `wc -l` check.

### Wins

- [project] **ListModel over JS array for activity log.** JS array as `ListView.model` doesn't support `.get(i)` (ListModel API). Copy-all iteration `model.get(i)` worked correctly with ListModel; JS array indexing failed silently. Pattern: always use `ListModel` for log panels, never JS `var` arrays.

- [project] **Keycard ActivityLog pattern direct reuse.** Importing the exact pattern from keycard (ListModel inside ListView, `clipboardHelper` inside the Rectangle, `copyAllToClipboard()` function on the Rectangle, `TextEdit readOnly + selectByMouse`) worked on first try in stash UI. Should port proven patterns directly instead of reinventing.

- [project] **`doStashBackup()` in notes drives the full flow.** Notes saves → exports `.imnotes` → calls stash `uploadViaIpfs` → records CID. Stash is a pure transport — it doesn't need to know what to pin. This separation is clean and avoids any cross-module scheduling complexity.

### Extracted to

- `basecamp-skills/skills/qml-patterns.md` — `background: null` on TextEdit trap; clipboard helper scope rule
- `basecamp-skills/skills/plugin-discovery.md` — `variant` file confirmed required for LogosApp
- `stash-basecamp/docs/stash-pinning-lessons.md` — Pinata v3 JWT/endpoint, CIDv0/v1, token masking, appendLog

---

## #74 Settings UI + capability token investigation (2026-04-16)

### What happened

1. **logosAPI member shadowing** — Added `LogosAPI* logosAPI = nullptr` to `NotesPlugin.h`. `PluginInterface` base class already declares it. The shadowed member was set in `initLogos()` while the framework read the base class one (null) → "Invalid response" on every Keycard unlock. Fix: remove the duplicate; the base class member is the right one.

2. **FUSE mount leak from crashed AppImage runs** — 246 stale FUSE mounts accumulated in `/tmp/.mount_curren*` from prior test sessions. Caused keycard module to fail resolving its squashfs path. Cleaned with `fusermount -u`. Lesson: add FUSE sweep to every kill-and-relaunch sequence.

3. **capability_module.requestModule() blocks and crashes** — Tried to call `capability_module.requestModule("notes", "storage_module")` synchronously in `ensureStorageClient()` (called from `initLogos()`). The `invokeRemoteMethod` default timeout is ~20s; the call blocked the notes host process thread and crashed the app. Reverted immediately. Synchronous IPC calls from module init paths are unsafe.

4. **C++ plugin → storage_module IPC requires capability token** — `LogosAPIClient::isConnected()` returns false for storage_module even when the module is running and was loaded first. The direct C++ plugin IPC path is blocked without capability provisioning (#77). "Logos Storage unavailable" in the UI is correct and honest.

### Root causes

- logosAPI: didn't check the base class header before adding a member with the same name
- FUSE leak: kill sequence in CLAUDE.md didn't include `fusermount -u` sweep
- capability crash: assumed `invokeRemoteMethod` was safe to call from init path without checking timeout/blocking behavior
- IPC limitation: capability token requirement was known (#77) but worth confirming empirically

### What went right

- Reverted the crashing capability call immediately; didn't try to work around it
- FUSE sweep fix is now documented in basecamp-skills
- "Logos Storage unavailable" text is honest — no false "connected" state shown to user
- The UI panel itself (status dot, CID, timestamp, Back up now) is complete and correct

### Lessons added to basecamp-skills

- Kill sequence now includes `fusermount -u` sweep (platform-module-structure.md)

### Open items

- #77: Research capability token provisioning — async approach needed (not from init path)
- #78: QML routing workaround as alternative to C++ direct IPC

---

## Manual test setup — wrong AppImage + wrong build dir (2026-04-16)

Session start. Alisher asked for a manual test. Launched wrong AppImage and built from wrong directory.

### What happened

1. Ran `cmake --build build && cmake --install build` — used the old `build/` directory, not `build-new/`
2. Launched `~/logos-app/result/logos-basecamp.AppImage` (Nix result, commit `ce48695`) — not the correct `logos-basecamp-9b52529-160.AppImage`

### Root cause

- Relied on `CLAUDE.md` AppImage path (`~/logos-app/result/logos-basecamp.AppImage`) without verifying it was current
- halt.md was stale — no guidance on which AppImage or build dir to use after 5 commits landed
- Did NOT consult skills docs before launching, despite user prompt "explore basecamp skills" being an explicit hint
- Opened the app before reading the skills that would have revealed the correct setup

### Why this matters

- Tested against the wrong AppImage version. Results would be meaningless or misleading.
- `build-new/` uses the SDK-upgraded build with nix develop; `build/` is the old system-library build. Different configs, different install targets.

### Fix

- Always consult `docs/skills/` before any test session — especially after a stale halt
- Correct AppImage: `~/.local/share/Logos/appimages/logos-basecamp-9b52529-160.AppImage` (or `current.AppImage` symlink)
- Correct build dir: `build-new/` (nix develop, SDK-upgraded)
- Verify install target before launching: `cmake --install build-new` → check which Logos dir it writes to

### Rule to add

> Before any test session, read `docs/skills/` (at minimum: `working-baseline.md`, `appimage-module-versioning.md`). Never assume the CLAUDE.md AppImage path is current.

---

## New AppImage Compat — UI modules investigation (2026-04-11)

Branch: `feature/new-appimage-compat` on logos-notes + keycard-basecamp. Not merged — UI modules still not showing.

### Process wins
- **Manifest format diff found fast.** Comparing the embedded `counter_qml` (inside AppImage) vs our manifests revealed the v0.2.0 breaking changes: `ui_qml` plugins need `"view": "Main.qml"` + `"main": {}`, and `metadata.json` must use `"view"` not `"main"`. Found this by reading `/tmp/.mount_logos-*/usr/plugins/counter_qml/manifest.json` directly.
- **Core modules now load without crash.** Both notes and keycard appear in module stats in the new AppImage after manifest updates. The v0.1.0 → v0.2.0 format was the crash root cause.
- **Source research gave precise answer.** logos-app `MainUIBackend.cpp` confirmed: sidebar uses `package_manager.getInstalledUiPluginsAsync()`, not file scan (since commit `113b67c`, Mar 27). Research took one agent call vs hours of guessing.

### Process fails
- [process] **Spent time updating metadata.json without first verifying that ANY user-installed plugin appears in the sidebar.** Counter_qml exists in the user dir with old format — if it appeared, old format works; if it didn't, the path is wrong. Should have used counter_qml as a known-working canary before touching our files.
- [process] **Installed notes_ui to `LogosBasecamp/plugins/` without confirming that's the correct user plugins path.** Research found the package_manager uses `setUserUiPluginsDirectory()` — we never found what value that is set to in the new AppImage. Could be `logos_host/plugins/` or `Logos/LogosBasecamp/plugins/` — unchecked.
- [process] **Two AppImage instances were running simultaneously during the test.** Kill command returned exit 1 (no processes), then launched a new one — but the old one was still alive. Discovered via `ps aux`. Always verify 0 logos processes before relaunching.
- [process] **Chased LGX install path before confirming directory scanning still works.** The research showed `setUserUiPluginsDirectory()` IS a directory scan (not DB-only). We built LGX packages and tried to install them before confirming whether the raw directory approach just had the wrong path.

### Project wins
- **Both manifest formats now correct in source.** Four manifest.json + two metadata.json files updated and committed on `feature/new-appimage-compat` branches.
- **New LGX packages built** at `~/Desktop/` from the updated branch.
- **AppImage architecture understood.** Two-layer discovery (embedded dir + user dir), controlled by `setEmbeddedUiPluginsDirectory()` + `setUserUiPluginsDirectory()` in `MainUIBackend.cpp` lines 93-127. Exact user dir path is the remaining unknown.

### Project fails
- [project] **UI modules still don't appear in sidebar.** Not yet confirmed whether cause is: wrong install path, LGX install failure, or something else in the package_manager's scan logic.
- [project] **Capability deny still unresolved.** Storage module still returns "false" for our plugin. Upstream question drafted but not posted.

### Additional process fail (post-research)
- [process] **Chained tmux-bridge calls with `&&`, causing Enter to be skipped.** After `tmux-bridge message`, the `&&` chain ran `tmux-bridge keys Enter` then `tmux-bridge read senty@logos-notes 5`. The blocking read failed with "must read the pane before interacting" — each tmux-bridge call consumes the read gate, so the Enter step was never confirmed delivered. Alisher caught it. Root cause: treating tmux-bridge as a normal shell pipeline instead of a stateful protocol where each call must be a separate command. Fix: protocol updated in `~/fieldcraft/protocols/builder-auditor.md` — always run message, keys Enter, and read-back as three separate commands, never chained.

### Root cause (found in overnight research)
**Stale user-installed `package_manager_plugin.so` (v0.1.0) missing the new API.**
Since commit `113b67c`, `MainUIBackend` calls `setUserUiPluginsDirectory()` + `getInstalledUiPluginsAsync()`.
The old v0.1.0 module only had `setUiPluginsDirectory` (single dir) and no `getInstalledUiPlugins` at all.
New API calls silently did nothing — no error, empty sidebar. Fix: copy embedded AppImage v0.2.0 module.

Confirmed via `nm -D`: old module missing `_ZN17PackageManagerLib21getInstalledUiPluginsB5cxx11Ev` entirely.

### Project lessons
- **Use embedded AppImage plugins as canary before testing user-installed ones.** counter_qml is always in the AppImage. If it shows → discovery works. If it doesn't → something is fundamentally wrong with the sidebar, unrelated to our plugins.
- **Find the exact runtime path before copying files.** `grep -r setUserUiPluginsDirectory ~/logos-app/src/` would give the answer in 5 seconds. Don't assume `LogosBasecamp/plugins/` is correct.
- **Kill verification matters.** After `pkill`, always `ps aux | grep logos | grep -v grep | wc -l` before relaunching. A stale instance will pollute module stats.

---

## SDK upgrade — logos-cpp-sdk Feb 25 → Apr 9 (2026-04-10)

Merge: `97c3b3f`. Single-commit merge (flake.lock only).

### Process wins
- **Peer module comparison unlocked the diagnosis.** Cloning tictactoe module by @fryorcraken and comparing SDK versions revealed ours was a month behind (Feb 25 vs Mar 23). The new SDK has 10 additional headers including the entire `LogosObject` / registry / transport layer. This reframed the #77 capability-token deny from "permission issue we can't control" to "possible version mismatch we CAN fix."
- **Dedicated branch preserved master throughout.** `feature/sdk-upgrade` branched from master, built in `build-new/`, tested inside nix develop. Master was never at risk. Alisher explicitly requested this safety and it was the right call — if the upgrade had broken things, we'd have reverted cleanly.
- **Discovered the nix develop test requirement immediately.** First test run outside nix shell failed with `libzstd.so.1` not found. Diagnosed in one step (ldd inside shell), fixed by running all tests via `nix develop -c bash -c "ctest"`. Documented in `docs/skills/sdk-upgrade-guide.md` for keycard-basecamp and future modules.

### Process fails
- [process] **Spent 14 rounds debugging #77 on the old SDK before checking version.** The SDK version comparison took 5 minutes and revealed a likely root cause. Should have been the FIRST diagnostic, not the last. When cross-module IPC fails in a framework you don't control, check your SDK version before writing workaround architectures.
- [process] **Built and committed an entire QML-routing workaround (#78) based on the assumption that the capability system was intentionally blocking us.** The assumption was "the framework denies our access by design." The reality may be "the framework can't find our module because we're using an incompatible registry protocol." A 5-minute version check would have saved 4+ hours of #78 work.

### Project wins
- **New SDK is backward-compatible with all existing code.** Zero compile errors, zero new warnings, 7/7 tests pass. The `LogosObject*` / 3-arg `onEvent` changes only affect `LogosStorageTransport.cpp` which is the transport layer — the rest of the codebase doesn't reference the SDK directly.
- **SDK upgrade guide created** at `docs/skills/sdk-upgrade-guide.md` with the full procedure, version table, nix develop gotcha, and checklist. Directly reusable by keycard-basecamp module.
- **Tictactoe AI opponent PR submitted** (https://github.com/fryorcraken/logos-module-tictactoe/pull/1) — good-faith contribution to a peer builder, opens a communication channel for the capability-token question.

### Project fails
- [project] **Still haven't verified whether the new SDK resolves the capability-token deny.** The SDK is merged but Phase 2 code isn't rebased onto it yet. The critical test (does `invokeRemoteMethod` to storage_module now return a valid response?) is pending next session.
- [project] **Three investigation docs committed only to `feature/v2-autobackup`, not master.** `docs/skills/logos-capability-tokens.md`, `docs/upstream/basecamp-capability-tokens-questions.md`, and `docs/upstream/issue-draft-*.md` are on the reference branch but not available on master. Should cherry-pick the docs even if the code doesn't merge.

### Project lessons
- **Check SDK version against working peers FIRST when framework-level IPC fails.** This is now rule #1 in `docs/skills/logos-capability-tokens.md`. Before writing ANY workaround, compare your SDK rev/date against a module that works.
- **The logos-cpp-sdk has no semver.** Version is always `0.1.0` in the manifest. The only way to tell versions apart is the nix store hash, the git rev, or the header count (7 = old, 17 = new). Document the rev + date in every commit that changes the SDK.
- **`nix develop` is mandatory for tests after SDK upgrade.** New transitive deps (`libzstd`, `libglib`) aren't in the system library path. Running `ctest` outside the nix shell silently fails all tests with shared library errors.
- **Static linking of `liblogos_sdk.a` means no ABI conflict.** Each module carries its own SDK copy. A plugin built with the Apr 9 SDK loads fine in a Basecamp AppImage built with an older SDK. This is safe.

---

## Issue #71 — v2.0 Phase 1: StorageClient (2026-04-09)

Merge: `a7f327a`. Single-issue merge (not epic), so skills extraction + wins/fails only.

### Process wins
- **Transport abstraction for testable IPC.** Defined a `StorageTransport` interface so `StorageClient` core has zero dependency on the Logos SDK. Tests use a `MockStorageTransport` with no SDK linkage. Real implementation (`LogosStorageTransport`) is a separate TU compiled only into the plugin build. 24 unit tests run in 0.4 seconds with no external setup.
- **Senty caught two real MEDIUMs in round 3.** Pending callbacks could hang forever without a terminal event, and FIFO-by-event-name routing silently misroutes on overlap or stray events. Both were textbook async-IPC holes that unit tests wouldn't have naturally exercised. The round-4 fix (`std::optional` pending slots + per-request `QTimer`) closed both with a coherent single-in-flight contract.
- **Planning discipline paid off.** Senty's round-1 review forced the Keycard-only scope and the destructive-restore contract split. Without that, Phase 1 would have shipped against a muddier target and required rework in Phase 3.

### Process fails
- [process] **Falsely flagged the `storage_module` install as an upstream blocker.** Saw `storage_module_plugin.so` in the legacy `~/.local/share/Logos/LogosApp/modules/` dir, classified it as "legacy not available in Basecamp", drafted a 4-option decision memo, and halted Phase 1 implementation. The correct action was to try the trivial experiment first: `cp -r` from LogosApp to LogosBasecamp. That worked on the first try — the v1.2.0 change was a rename, not an ABI break. Alisher had to say "research and install" to get me to investigate. Root cause: treated "found in legacy dir" as equivalent to "not installed anywhere" without verifying. Also missed the clue in Alisher's phrasing "I recall using storage" — strong signal it existed locally.
- [process] **`tmux-bridge type/message` silently dropped input during the Phase 1 handoff.** Commands returned exit 0 but text never landed in Senty's pane. The earlier tmux-bridge feedback memory ("follow message with keys Enter") only covers half the failure mode. Worked around with native `tmux send-keys -t %2 "..."; tmux send-keys -t %2 Enter; tmux capture-pane -pt %2` — verified reliable. Root cause unknown, tmux-bridge source needs investigation.
- [process] **Skipped post-merge protocol after Phase 1 merge.** Went straight from merge into Phase 2 planning. Alisher had to remind me to run skills extraction + wins/fails logging. Root cause: treated the merge as "task done" instead of "trigger for post-merge discipline". The autonomous merge criteria are necessary but not sufficient — they tell you when you CAN merge, not what to DO after merging.
- [process] **Asked Alisher three open questions in one message after the merge** (proceed to Phase 2 / push / remove storage_ui). Should have had a default action plan and only asked about the decisions that actually required input.

### Project wins
- **Phase 1 is purely additive.** No renames, no caller audits, no migrations. Existing `importBackup()` stays untouched since its only callers are fresh-DB account-import flows. This dropped a significant amount of churn from the original Phase 3 plan after Alisher clarified "POC, no backwards compat needed".
- **`storage_module` lives in `LogosApp/`.** The legacy install dir still has the compatible .so files after the v1.2.0 rename. Copying them to `LogosBasecamp/` unblocks Phase 2/3 integration without needing to build from source.

### Project fails
- [project] **`storage_ui.so` breaks the Basecamp sidebar** when opened. After tapping the Storage module, all other sidebar entries disappear and the Modules page shows no UI modules. Pre-existing LogosBasecamp/storage_ui bug, not ours. Workaround: removed `storage_ui` from `~/.local/share/Logos/LogosBasecamp/plugins/` — we only need `storage_module` (the backend) for Phases 2/3 since we invoke it via `LogosAPIClient::invokeRemoteMethod`, not via the user visiting the Storage page.
- [project] **Legacy `libkeycard.so` + `libpcsclite.so.1` still in `LogosBasecamp/modules/notes/`.** These were removed from the source tree in Epic #62 (keycard-basecamp migration) but the old files persist in the installed module dir because `cmake --install` only adds, never removes. Not breaking anything but a stale-state hazard for future debugging.

### Project lessons
- **Transport abstractions are the right move for IPC-heavy code.** One tiny virtual interface (`StorageTransport`) made the 24 unit tests possible without linking any of the Logos SDK. Pattern worth reusing for future cross-module consumers on the notes plugin side.
- **"storage_module not installed" is recoverable by copy** — the v1.2.0 rename did not break ABI. Any other "missing" module should be searched for in sibling Logos dirs before escalating.
- **Single-in-flight invariants eliminate entire classes of async bugs.** The combination of `std::optional` pending slots + per-request timer means a timeout cannot race with a late success, a stray event cannot misroute, and concurrent calls fail synchronously with a clear error. Much safer than FIFO-by-event-name matching, for zero additional code.
- **Residual integration risk flagged for Phase 2:** the exact shape of `storageUploadDone` / `storageDownloadDone` args is still assumption-based. When Phase 2 wires up the real IPC path and we observe events against the installed storage_module, the extraction logic in `StorageClient::onEventResponse` may need adjustment. Marked in the source as a TODO for the first real integration test.

### Feedback for Alisher
- Caught the post-merge protocol miss immediately — agents need that correction to internalize it or it'll repeat on every phase merge.
- "POC, no backwards compat needed" was the correct scope call and dropped meaningful complexity.
- "I recall using storage" was the right nudge — next time I'll treat recall-phrases as evidence and search harder before escalating.

---

## Issue #61 — Cleanup Legacy References (2026-04-09)

### Process wins
- Builder-auditor loop caught two real MEDIUM regressions before merge (stale docs claiming artifacts removed when they weren't, broken `package-lgx` workflow)
- Round 2 was clean — fixes were concrete and fully verified
- Manual Basecamp test confirmed app works end-to-end after cleanup

### Process fails
- [process] **Fergie committed directly on master.** Alisher caught it. Root cause: skipped the branch creation step in the protocol.
- [process] **Fergie claimed artifacts were removed without verifying filesystem.** Ran `ls` from `build/` subdirectory, got "not found", assumed files were gone. Senty caught it in Round 1.
- [process] **Senty's first review only went via tmux, not GitHub.** Alisher corrected — GitHub comment first, then tmux ping.
- [process] **Fergie used tmux-bridge message without the full read-message-read-Enter cycle.** Message sat unsubmitted until Alisher pointed it out.
- [project] **CLAUDE.md had wrong AppImage path** (`result/bin/` vs `result/`). Fergie followed it blindly instead of verifying with `ls`.

### Project lessons
- Cleanup commits need repo-wide search + filesystem verification, not just diff reading
- Removing a command/script requires updating every workflow doc and help string in the same commit
- Always `ls` a documented path before executing it — docs go stale

### Feedback for Alisher
- Catching the master commit early saved a messy rebase
- Pushing us to follow tmux-bridge protocol properly — agents need the correction to internalize the cycle

---

## Fieldcraft Retrofit Trial Sprint (2026-04-08)

### Process wins
- `fieldcraft-session` script works — one command launches full workspace
- Per-project background tint works — visual distinction between projects
- Senty found real inaccuracy in docs (#69) — review loop caught something
- Both agents completed work on the retrofit branch without breaking anything
- Fergie correctly skipped Senty review for docs-only scope as written in issue
- Issue-driven workflow (#69, #70) kept scope clear

### Process fails
- **Agents didn't self-initialize fieldcraft protocols.** Neither Fergie nor Senty read `~/fieldcraft/agents/*.md` or `~/fieldcraft/protocols/*.md` without explicit prompting. CLAUDE.md and CODEX.md reference them, but agents need to be told "read your fieldcraft identity" at first launch.
- **tmux-bridge pane labels collided across projects.** `fergie` resolved to wrong session when stale sessions existed. Fixed: labels now namespaced as `fergie@project-name`.
- **Stale tmux sessions accumulate.** Closing terminal detaches but doesn't kill. Users must explicitly `fieldcraft-stop`. Added to RUNBOOK.
- **Fish theme approach failed.** Starship prompt controls visible colors, not fish color vars. Wasted 3 iterations before diagnosing. Per-project background tint via tmux was the right solution.
- **Senty's tmux-bridge ping to Fergie didn't land.** Likely resolved to stale session pane. Namespaced labels should fix this going forward.

### Action items for next sprint
1. **CLAUDE.md and CODEX.md must explicitly instruct agents to read their fieldcraft agent file and protocols at session start** — not just reference them, but say "read these files now"
2. **Agents need the namespaced label format** — update protocols to use `fergie@{project}` pattern in tmux-bridge examples
3. **Test the full cold-start** — next sprint, give zero extra context beyond "read CLAUDE.md" and see if agents follow the full chain

### Feedback for Alisher
- Good instinct catching stale crypto docs before merge — that would have been a real problem
- Testing the script live and iterating was efficient — 4 iterations to working state
- Relaying between agents manually worked but shouldn't be needed — namespaced labels should fix

---

## Week of 2026-04-09

### Wins
- [process] The builder-auditor loop caught two real cleanup regressions before merge: removed flake apps still had live doc/help references, and "artifacts removed" claims did not match the actual filesystem.
- [process] The second review pass converged cleanly because the fixes were narrow, concrete, and re-verified with build plus test coverage.
- [project] The repo is now actually rid of the dead keycard bridge leftovers (`lib/keycard/`, `build-libkeycard.sh`, `fix-lgx.sh`, `package-lgx.sh`) instead of merely documenting them as gone.

### Fails
- [process] Review handoff discipline slipped on the first pass: findings were sent over tmux before they were posted to GitHub. The required order is GitHub comment first, then tmux ping.
- [process] Cleanup assertions were treated as true before verification. Both implementation and review need repo-wide search plus filesystem checks for "removed" or "unused" claims.
- [project] User-facing build instructions drifted from the actual flake surface. Removing `package-lgx` without updating `CLAUDE.md` and the flake shell help would have broken the documented release workflow.

---

<!-- Template:
## Epic #NN — Title (YYYY-MM-DD)

### Process wins

### Process fails

### Project lessons
(added to docs/skills/lessons.md)

### Feedback for Alisher
-->
