# Fergie/Senty Collaboration Workflow

Multi-agent development workflow for logos-notes using two AI agents:
- **Fergie (Claude):** Implementation agent - builds features
- **Senty (Codex):** Review agent - catches issues, validates quality
- **User:** Product owner - orchestrates, makes final decisions

---

## Session Setup (one-time per work session)

Label panes so agents can find each other by name:

```bash
# Run in Fergie's pane
tmux-bridge name "$(tmux-bridge id)" fergie

# Run in Senty's pane
tmux-bridge name "$(tmux-bridge id)" senty

# Verify both are visible
tmux-bridge list
```

---

## Standard Issue Workflow

### 1. Issue Creation & Planning

**User creates issue** (or selects from backlog)

**Fergie checks issue:**
```bash
gh issue view XX
```

**Fergie confirms understanding:**
- Read issue body, requirements, success criteria
- Check dependencies and blockers
- Ask clarifying questions if needed

---

### 2. Implementation (Fergie)

**Create feature branch:**
```bash
git checkout master
git pull origin master
git checkout -b issue-XX-feature-name
```

**Implement changes:**
- Write code following PROJECT_KNOWLEDGE.md lessons
- Test locally (build, install, run)
- Fix issues as they arise

**Commit changes:**
```bash
git add <files>
git commit -m "Brief description

Detailed explanation of changes...

Related to Issue #XX

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

**Push to remote:**
```bash
git push origin issue-XX-feature-name
```

---

### 3. Handoff to Senty

**Fergie posts handoff comment (permanent record):**
```bash
gh issue comment XX --body "Fergie: Ready for review! 🎯

## What's Implemented
- Feature 1
- Feature 2

## Testing
✅ Build succeeds
✅ Tests pass

## Files Changed
- path/to/file.cpp

Branch: issue-XX-feature-name"
```

**Fergie notifies Senty via tmux-bridge:**
```bash
tmux-bridge read senty 20
tmux-bridge message senty '/btw check issue #XX'
tmux-bridge read senty 20
tmux-bridge keys senty Enter
```

---

### 4. Review (Senty)

**Senty reads the issue:**
```bash
gh issue view XX
```

**Senty reviews code:**
- Validates against requirements
- Checks security, correctness, quality

**Senty posts review comment (permanent record):**
```bash
gh issue comment XX --body "Senty:

Findings:
1. [SEVERITY] - Issue description
2. [SEVERITY] - Another issue

Result: [LGTM / not LGTM yet]"
```

**Senty notifies Fergie via tmux-bridge:**
```bash
tmux-bridge read fergie 20
tmux-bridge message fergie '/btw check issue #XX'
tmux-bridge read fergie 20
tmux-bridge keys fergie Enter
```

---

### 5. Fixes (If Needed)

**Fergie reads Senty's findings:**
```bash
gh issue view XX
```

**Fergie makes fixes → posts update comment → notifies Senty:**
```bash
gh issue comment XX --body "Fergie: Fixed P1 (bounds check in NotesBackend.cpp:142). Ready for re-review."

tmux-bridge read senty 20
tmux-bridge message senty '/btw check issue #XX'
tmux-bridge read senty 20
tmux-bridge keys senty Enter
```

**Repeat until LGTM.**

---

### 6. Merge Sub-issues to Epic Branch (After LGTM)

After each sub-issue gets Senty's LGTM, commit it to the epic branch. **Do not merge to master yet.**

### 7. User Testing Gate

When all sub-issues are committed to the epic branch:
1. Push the branch
2. Notify the user that the branch is ready for manual testing
3. **Wait for user confirmation** — do not merge to master without it

### 7b. Post-Testing Fix Round

After user testing reveals bugs:
1. Fix each bug and commit to the branch
2. **Post a summary on the epic issue** listing all problems found + fixes applied + commit SHAs
3. **Ping Senty for review** of the fixes before proceeding to merge
4. Wait for Senty LGTM on the fix round
5. If user needs to re-test, repeat from step 7

### 8. Merge to Master (After User Test + Fix Review)

Only after user confirms the branch works:
```bash
gh pr create --title "Epic #XX: Feature" --base master
gh pr merge XX --squash --delete-branch
```

### 9. Document Lessons (Immediately After Merge)

**This step is mandatory — do not wait for a prompt.**

After every merge to master:
1. Add lessons to `PROJECT_KNOWLEDGE.md` under a section for the epic/issue
2. Commit and push the lessons to master
3. Cover: what went wrong, what Senty caught, workflow improvements, system-level findings

### 10. Auto Retro (After Every Epic Merge)

**This step is mandatory — do not skip.**

After every epic merge to master:
1. Discuss with Senty via tmux-bridge: what worked, what didn't, improvements
2. Share feedback for the user (orchestration, communication, scope clarity)
3. Update WORKFLOW.md with any new rules
4. Commit retro outcomes

---

## LGX Release Workflow

Every LGX build request must go through an issue:

### 1. Create Release Issue
```bash
gh issue create --title "Release: Build LGX packages for <version/description>"
```
Include: what's in the release, what changed since last build, known issues.

### 2. Build
```bash
mkdir -p dist && nix run .#package-lgx -- dist/
```

### 3. Verify
- notes_plugin.so present in core LGX
- UI has Main.qml and all dependencies
- Sizes are reasonable

### 4. Post Results on Issue
Comment with artifact sizes, contents verification, and any fixes applied during build.

### 5. Senty Review
Ping Senty for sign-off before release is final.

---

## Notification Protocol

- **GitHub is the system of record.** Every substantive review result, finding, fix handoff, and final LGTM must be posted on the relevant issue/PR.
- **tmux-bridge is interrupt + routing only.** Use it to notify the other agent that attention is needed; do not treat tmux as the durable record.
- **Do not poll GitHub comment counts or sleep-wait for responses.** After sending a tmux ping, continue useful work. The receiving agent will reply via tmux-bridge when ready.

### Handoff Status Tags

| Tag | Meaning |
|-----|---------|
| `READY` | All issue success criteria for the current scope are complete and ready for review |
| `PARTIAL` | Code or validation is incomplete; review requested only on the completed subset. State what is still pending |
| `FIX` | Prior review findings have been addressed and the issue is ready for re-review |
| `RECHECK` | No code change expected; reviewer should verify updated evidence, runtime result, or issue-thread clarification |
| `BLOCKED` | Cannot proceed without input, permission, missing dependency, or failed prerequisite |

### Handoff Message Format

Use tmux pings in this shape: `/btw [TAG] check issue #NN — one-line scope summary`

### Required Handoff Checklist

- Every `READY`, `PARTIAL`, or `FIX` handoff must include the issue success criteria as a short checklist in the GitHub comment, with each item marked done or not done.
- If any criterion is still open, the handoff must be `PARTIAL`, not `READY`.
- If the issue has a repo-default verification matrix, the handoff comment must explicitly state each verified surface.

### Default Verification Matrix

Unless the issue says otherwise, verify:
1. `nix develop` dev build (`cmake -B build -G Ninja && cmake --build build`)
2. `nix build .#lib`
3. `cd build && ctest --output-on-failure`

Host runtime validation in Basecamp is required **only** when the issue explicitly calls for real host-app behavior.

### Every GitHub Update Gets a Ping

- If you post a new comment or addendum on an issue, you **must** ping the other agent via tmux-bridge — even if the issue was already LGTM'd.
- An un-pinged update is invisible to the other agent. They will not check GitHub unprompted.

### Batching Rules

- If multiple issues are independently reviewable at once, batch them into one tmux ping instead of sending multiple separate nudges.
- The reviewer may still post separate GitHub comments per issue if the findings differ by scope or severity.

### Reviewer Response Protocol

- Reviewer posts findings or LGTM on GitHub first, then sends a tmux ping back referencing the issue(s).
- Implementer treats the tmux ping as the callback signal and should not poll GitHub while waiting.

### Security/Storage Handoff Checklist

For issues that modify encryption, storage, or security-critical paths, the handoff must include a **threat-path checklist**:
- [ ] Read path: what happens on success, wrong key, missing file, corruption?
- [ ] Write path: does it fail-closed on corruption? Can it overwrite unreadable data?
- [ ] Migration path: does it only run after the validating operation succeeds?
- [ ] Cache/memory path: when is sensitive data cleared? (wrong PIN, card loss, session close, unpair)
- [ ] Fail-closed question: on parse error / corruption / missing dependency, do we stop safely or rewrite state?

### API Change Handoff Checklist

For issues that change method signatures or state contracts:
- [ ] Caller audit: grep all callers (QML, C++, tests) and verify they use the new signature
- [ ] Expected visible behavior change: name what the user or consuming module sees differently, not just the code diff

### Blocked Issue Protocol

Once a blocker is confirmed:
1. File the upstream issue (with agent attribution)
2. Link the upstream issue from the local issue
3. Freeze local scope — stop speculative local investigation rounds
4. Handoff template: **locally verified** / **upstream dependency** / **exact next action and owner**

### Verification Commands

- All verification commands in handoff comments must be **copy-pastable, exact, and repo-rooted**
- If a rerun needs a special cwd, env var, or setup step, state it explicitly

---

## Clarification Triggers

When the user's input touches any of the following topics, **stop and request clarification** before proceeding:

### A) Security or contract-sensitive work
If the task involves encryption, key handling, storage security, or API contracts:
- **Ask for the threat model** — even 2-3 bullets (e.g., "attacker has filesystem access but not the card")
- Do not infer security requirements — request them explicitly

### B) Ambiguous success conditions
If the task description doesn't specify what "done" looks like:
- **Ask for operational success criteria** — e.g., "encrypted on disk" vs "fixed"
- Clarify: code-only, runtime proof, upstream issue, package produced?

### C) Potentially blocked or upstream-dependent work
If the task may depend on external repos, unreleased features, or untested environments:
- **Ask for a stop condition** — e.g., "try two paths, if neither works, file upstream and stop"
- Do not over-investigate locally without a boundary

### D) Autonomous execution scope
If the user says "go ahead" or "follow protocol without asking":
- **Confirm which actions are pre-approved** — GitHub comments, pings, issue triage, merges?
- When in doubt, ask once up front rather than interrupting mid-flow

**Both Fergie and Senty must follow these triggers.** When triggered, notify the user and request the missing context before continuing.

---

## Communication Protocol

- **Fergie comments:** Start with `Fergie:`
- **Senty comments:** Start with `Senty:`
- **User comments:** No prefix

---

## Branch Workflow

- **master:** Stable, production-ready
- **issue-XX-feature:** Active development
- **Never commit directly to master** (except docs after merge)
- **Squash merge** to master, delete branch after

---

## Success Checklist

**Before handoff:**
- [ ] Code builds (dev build + nix build .#lib)
- [ ] Tests pass (ctest)
- [ ] Branch pushed
- [ ] Handoff comment posted on GitHub with success criteria checklist
- [ ] Agent notified via tmux-bridge with correct status tag

**Before merge:**
- [ ] Senty LGTM received
- [ ] PR created
- [ ] PR merged
- [ ] Lessons documented
- [ ] Auto retro completed (for epics)
