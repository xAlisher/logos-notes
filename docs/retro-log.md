# Retro Log

Post-merge retrospectives per `~/fieldcraft/protocols/wins-and-fails.md`.

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

<!-- Template:
## Epic #NN — Title (YYYY-MM-DD)

### Process wins

### Process fails

### Project lessons
(added to docs/skills/lessons.md)

### Feedback for Alisher
-->
