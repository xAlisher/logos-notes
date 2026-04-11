# Halt — 2026-04-11 (probe-first plan approved — Phase 0 ready)

## Where we stopped

Epic #79 restructured as a probe-first investigation. Plan written, sub-issues created,
Senty reviewed 2 rounds and gave LGTM on `5077776`. Ready to execute Phase 0.

## Current state

- **Branch:** `feature/new-appimage-compat`
- **Last commit:** `5077776` — docs: apply Senty Round 1 findings to probe-first plan
- **Build:** 7/7 tests pass on master (build-new/)
- **Open review:** none — Senty LGTM given, safe to proceed

## Plan structure

- **Epic:** #79 (updated with sub-issues)
- **Plan doc:** `docs/plans/probe-first-appimage-compat.md`
- **Sub-issues:**
  - #80 — Phase 0: Log working state + revert capability_module + package_manager
  - #81 — Phase 1: Clean tictactoe baseline test
  - #82 — Phase 2: Build probe_ui introspection plugin
  - #83 — Phase 3: Run probe, interpret, decide fix strategy

## Next steps (in order)

1. **Execute Phase 0 (#80):**
   - Log AppImage git SHA (`cd ~/logos-app && git log --oneline -3`)
   - Log master state + run tests
   - Launch AppImage briefly to get mount, then restore BOTH:
     ```bash
     MOUNT=$(ls /tmp | grep ".mount_logos-" | head -1)
     cp /tmp/$MOUNT/usr/modules/capability_module/capability_module_plugin.so \
        ~/.local/share/Logos/LogosBasecamp/modules/capability_module/
     cp /tmp/$MOUNT/usr/modules/package_manager/package_manager_plugin.so \
        ~/.local/share/Logos/LogosBasecamp/modules/package_manager/
     ```
   - Log full module inventory from `~/.local/share/Logos/LogosBasecamp/modules/`
   - Write `docs/skills/working-baseline.md`
   - Commit, close #80

2. **Execute Phase 1 (#81):** Clean tictactoe baseline test (remove all but tictactoe modules, launch, tap 3x)

3. **Execute Phase 2 (#82):** Build probe_ui in `~/logos-probe/` (separate dir, tictactoe mirror)

4. **Execute Phase 3 (#83):** Run all probe buttons, record output, apply decision gate

## Blockers

- Phase 0 and Phase 1 require manual UI actions by Alisher (launching AppImage, tapping modules)
- Phase 2 is fully automated (Fergie builds probe_ui)

## Context that's hard to re-derive

- Senty's key finding: tictactoe baseline can false-fail if `package_manager` is stale user copy
  (v0.1.0 overrides AppImage). Must restore BOTH framework modules, not just capability_module.
- Senty's second finding: probe passing ≠ notes_ui startup clean. If probe works but notes_ui
  stalls → diff constructor/createWidget/initLogos ordering vs tictactoe.
- The patched capability_module source is still in `/tmp/capability_module_fix/` (may be cleaned).
  Installed patched copy at `~/.local/share/Logos/LogosBasecamp/modules/capability_module/` —
  this is exactly what Phase 0 reverts.
