# Shared Memory: How Two AIs Stay in Sync Without a Human Relay

Five blog posts, four security review rounds, nine hotfixes, and a growing CLAUDE.md that had become 900 lines long. Something had to change.

This post is about restructuring how project knowledge flows between two AI agents — and removing myself as the bottleneck.

## The problem with one big file

CLAUDE.md started as a simple project brief. Here's what we're building, here's the tech stack, here's what's in scope. Fifty lines. Clear.

By v0.5.0 it was a monolith. Build commands next to encryption architecture next to lessons learned next to storage module research next to development routines next to the full Q_INVOKABLE API surface. Every new discovery got appended. Nothing got removed.

Two agents were reading this file — Claude Code (the implementer) and Codex (the reviewer). Both needed the project context. Neither needed the other's instructions. And every session started with both agents parsing 900 lines to find the 50 that mattered to them.

Worse: when one agent learned something (a sandbox restriction, a build quirk, a security finding), that knowledge only reached the other agent if I manually relayed it. I was the bottleneck. Copy a Codex finding into a Claude Code session. Paste a Claude Code fix summary back to Codex. Repeat.

## The split

Three files, three purposes:

**PROJECT_KNOWLEDGE.md** — shared mutable state. Current phase, architecture, open security findings, open questions, lessons learned, research notes, roadmap. Both agents read it at session start. Both agents write to it before session end. This is the relay — not me.

**CLAUDE.md** — Claude Code's instructions only. Build commands, development routines, merge criteria, encryption rules, integration patterns. Doesn't change unless the workflow changes.

**CODEX.md** — Codex's instructions only. What to review, severity levels, how to post findings, tie-breaking rules. Same principle: stable unless the process evolves.

One more file fell out naturally:

**ALISHER.md** — my own operating instructions. Five lines that say: you're the architect, not the relay. Check in with `git pull && cat PROJECT_KNOWLEDGE.md`. Make a call when agents get stuck. That's it.

## What "shared memory" actually means

The key insight is that PROJECT_KNOWLEDGE.md is not documentation. It's not a README. It's a live state file that agents treat as ground truth.

When Claude Code discovers that `ui_qml` plugins can't use `FileDialog`, it adds a lesson to PROJECT_KNOWLEDGE.md. Next time Codex reviews a PR that touches file I/O, it reads that lesson and knows to flag any `FileDialog` usage in plugin QML. No one had to tell Codex. The knowledge was already there.

When Codex finds a security issue, it adds it to the Open Security Findings table with a severity level. Claude Code reads that table at session start and knows what needs fixing before it starts new work. No one had to paste the finding into a Claude Code session.

The session close rule makes this work: before ending any session, update PROJECT_KNOWLEDGE.md. Add new lessons, mark resolved findings, note open questions. Commit it. The next session — by either agent — starts with current state.

## What got better

**Context window efficiency.** Claude Code's instruction file went from 900 lines to 283. Codex's went from a sprawling guide with inline project state to 196 focused lines. Both agents spend less context on instructions and more on the actual task.

**No more relay duty.** I used to spend the first ten minutes of each session copying findings between agents. Now I run `git pull` and both agents are current. The findings, the lessons, the open questions — all in one file that both can read and write.

**Cleaner boundaries.** Instructions don't drift with project state. When the workflow changes (new merge criteria, new review rules), I update the instruction files. When the project state changes (new finding, resolved question, completed phase), agents update PROJECT_KNOWLEDGE.md themselves.

**Audit trail.** Every change to shared knowledge is a git commit. `git log PROJECT_KNOWLEDGE.md` shows exactly when each lesson was learned, each finding was resolved, each question was answered. The commit messages follow a pattern: `docs: update PROJECT_KNOWLEDGE.md — <summary>`.

## What I learned

The restructuring itself was done by Claude Code and reviewed by Codex. Claude Code drafted the split, I reviewed for gaps and misplaced content, Codex tightened the language. Three rounds. The `darwin-arm64` typo kept getting reverted — four times — which is its own lesson about checking the full diff, not just the sections you edited.

The bigger lesson: AI agents don't need you to be their message bus. Give them a shared file with clear read/write rules, and they'll keep each other informed. Your job shifts from relay to architect — make decisions when they're stuck, set direction when it's ambiguous, and otherwise stay out of the loop.

The file structure now:

```
CLAUDE.md              — implementer instructions (stable)
CODEX.md               — reviewer instructions (stable)
PROJECT_KNOWLEDGE.md   — shared state (updated every session)
ALISHER.md             — architect instructions (5 lines)
```

Four files. No monolith. No relay. The agents talk to each other through the repo.

---

*This is part of the [Immutable Notes](https://github.com/xAlisher/logos-notes) build log. Previous: [Hotfixes and the Sandbox You Don't See](2026-03-15-hotfixes-and-sandbox-lessons.md).*
