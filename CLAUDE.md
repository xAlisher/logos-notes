# Immutable Notes — Claude Code Instructions

> Read PROJECT_KNOWLEDGE.md first. It contains current project state, open findings,
> and roadmap. This file contains only your instructions and rules.

---

## Identity & Protocols

You are **Fergie**. **Read `~/fieldcraft/agents/fergie.md` now** — it defines your identity, profile, and communication style.

**At session start, read these protocols from `~/fieldcraft/protocols/`:**
- `session-start.md` — how every session begins
- `builder-auditor.md` — review cycle with Senty (handoff format, severity levels, round rules)
- `halt-resume.md` — session pause/resume via halt.md

**Reference protocols (read when relevant):**
- `wins-and-fails.md` — capturing lessons after merges
- `clarification-triggers.md` — when to stop and ask before proceeding
- `upstream-attribution.md` — disclose AI agent on external issues
- `source-over-summaries.md` — re-read actual source, never work from own summaries
- `retro-after-merge.md` — auto retro with Senty after every epic merge

**tmux-bridge labels are project-namespaced.** Use `fergie@logos-notes`, `senty@logos-notes` in all tmux-bridge commands.

**Alisher sign-off required for:**
- Schema migrations
- Crypto primitive changes
- Major roadmap decisions (new phases, pivots)

Everything else: agents handle autonomously. Trust the loop.

---

## Tech Stack

- **Language**: C++17
- **UI**: Qt 6.9.3 QML at `~/Qt/6.9.3/gcc_64/`
- **Crypto**: libsodium 1.0.18 (AES-256-GCM, Argon2id, Ed25519)
- **Storage**: SQLite via Qt SQL
- **Build**: CMake 3.28 + Nix flake
- **OS**: Ubuntu 24.04

### Key headers (Nix store)
```
/nix/store/092zxk8qbm9zxqigq1z0a5l901a068cz-logos-liblogos-headers-0.1.0/include/interface.h
/nix/store/047dmhc4gi7yib02i1fbwidxpksqvcc2-logos-cpp-sdk/include/cpp/logos_api.h
```

---

## Build Commands

```bash
# Configure
cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=~/Qt/6.9.3/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j4

# Install to Logos App module paths
cmake --install build

# Run tests — always from build/ directory
cd build && ctest --output-on-failure

# Lint QML before installing
~/Qt/6.9.3/gcc_64/bin/qmllint plugins/notes_ui/Main.qml

# Launch Logos Basecamp for testing
pkill -9 -f "logos_host.elf"; pkill -9 -f "LogosBasecamp.elf"
~/logos-app/result/bin/logos-basecamp.AppImage
```

---

## Logos App Integration Rules

### Plugin contract
`NotesPlugin` is the only surface QML can see via `logos.callModule`. Every method QML
needs must be `Q_INVOKABLE` on `NotesPlugin`. Missing methods fail silently — no error,
just null response.

**Rule**: before implementing any new method on `NotesBackend`, immediately add the
`Q_INVOKABLE` wrapper to `NotesPlugin`.

### callModule returns JSON
`logos.callModule("notes", "loadNote", [id])` returns JSON-wrapped text, not raw plaintext.
QML must `JSON.parse()` the result. On error it returns `{"error":"..."}`.

**Rule**: always `JSON.parse()` callModule results in QML before use.

### QML sandbox restrictions (ui_qml type)
- No `Logos.Theme` or `Logos.Controls` imports — hardcode hex palette values
- No `FileDialog` — all file I/O through C++ plugin methods
- No file I/O from QML directly

### AppImage only for testing
`nix build '.#app'` expects `.lgx` packages. `cmake --install` copies raw `.so` files
which only work with the portable/AppImage build.

**Rule**: always test with AppImage, never with local Nix build.

### Kill all processes before relaunch
logos_host child processes survive LogosBasecamp being killed and hold stale `.so` files.

**Rule**: `pkill -9 -f "logos_host.elf"; pkill -9 -f "LogosBasecamp.elf"` before every test.

---

## Encryption Rules

**Normalize mnemonic before all crypto.**
Call `normalizeMnemonic()` (NFKD + whitespace + lowercase) before key derivation,
fingerprint derivation, and backup re-derivation. Same phrase with different spacing
= different keys = data loss.

**AES-256-GCM only, fail-fast.**
`crypto_aead_aes256gcm_is_available()` checked at startup. If unavailable, app refuses
to start. No fallback, no alternative cipher.

**No sensitive data in plaintext on disk.**
Note content, titles, and mnemonic are all encrypted. Only ciphertext + nonces +
wrapped key + KDF salts in DB.

**Every encrypt call uses a fresh random nonce.**
Never reuse nonces.

**SecureBuffer for all key material.**
Temporary derived keys, PIN UTF-8, mnemonic UTF-8 — use `SecureBuffer` (RAII wrapper
with `sodium_memzero`), not raw `QByteArray`.

**Restore rollback on failure.**
`setInitialized()` must be called only after backup restore succeeds. On any failure,
call `resetAndWipe()` and return to import screen.

---

## Logos App Shell Patterns

```cpp
// LogosAPI surface
auto* client = logosAPI->getClient("package_manager");
QVariant result = client->invokeRemoteMethod("SomeObject", "someMethod", args);
client->onEvent(originObj, this, "eventName", [](auto name, auto data) { ... });
logosAPI->getProvider()->registerObject("NotesBackend", backendObj);
```

```qml
// QML bridge
logos.callModule("notes", "methodName", [arg1, arg2])
// returns JSON string — always JSON.parse() before use

// Theme colors (hardcoded — no Logos.Theme in ui_qml)
// backgroundPrimary: "#1A1A1F"
// backgroundSecondary: "#232328"
// accentOrange: "#FF7D46"
// textPrimary: "#FFFFFF"
// textSecondary: "#9B9BA6"
```

---

## Development Routines

### After every feature branch
1. Manual UI/UX test checklist (see below)
2. `cmake --build build && cmake --install build`
3. Kill processes, test in Logos App AppImage
4. Rebuild the current branch before `ctest` if tests, packaging, or build wiring changed
5. `cd build && ctest --output-on-failure` — all tests must pass
6. Merge to master (see autonomous merge criteria)
7. Update README.md + screenshots
8. Create GitHub release with version tag + attach LGX artifacts (`nix bundle --bundler github:logos-co/nix-bundle-lgx .#lib` and `.#ui`)
9. Write blog post in `blog/` — update `blog/README.md`
10. Post to X

### Security fix routine
1. Branch: `security/pX-fixes`
2. Research, share analysis before implementing
3. Implement fixes, run tests locally
4. Commit and push branch — **do NOT merge yet**
5. Comment on relevant GitHub issue: tag as `[Claude Code]`
   Comment must include:
   - exact branch tip SHA
   - exact commands run
   - what was verified
   - what was NOT verified
   - validation status for `Unit`, `Artifact`, `Integration`, and `UI`
6. Request Senty review — wait for `Reviewed by: Codex` comment
7. Read Senty's findings, fix follow-ups, push, re-comment
8. Repeat until Senty posts LGTM or "no new findings"
9. Apply autonomous merge criteria (see below)

### Autonomous merge criteria
Merge without waiting for Alisher when ALL are true:
- `ctest` passes (current registered test set, all cases green)
- No HIGH or MEDIUM findings open in PROJECT_KNOWLEDGE.md for this change
- Senty's comment on the branch contains "LGTM" or "no new findings"
- Not a schema migration or destructive change (those always need Alisher sign-off)
- Not a crypto primitive change (always needs Alisher sign-off)

### UI/UX test checklist

**Standalone app:**
- [ ] Fresh reset → import mnemonic → PIN accepted
- [ ] Write note → lock → unlock → note intact
- [ ] Multiple notes → create, switch, delete
- [ ] Settings → public key shows, Copy works
- [ ] Settings → Export Backup → file appears
- [ ] Settings → Remove Account → confirm → wipe
- [ ] Restore from backup → notes restored
- [ ] Wrong PIN 5x → lockout → wait → correct PIN works

**Logos App (never skip):**
- [ ] `cmake --install build` → kill Logos App → relaunch AppImage
- [ ] Notes module loads in sidebar
- [ ] Full flow: import → notes → lock → unlock → notes intact
- [ ] Backup export/import works via plugin bridge
- [ ] SQLite check: `sqlite3 ~/.local/share/logos_host/notes.db "SELECT hex(title_ciphertext) FROM notes LIMIT 1;"` → hex blob, not readable text

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

## Handoff Status Tags

| Tag | Meaning |
|-----|---------|
| `READY` | All issue success criteria for the current scope are complete and ready for review |
| `PARTIAL` | Code or validation is incomplete; review requested only on the completed subset. State what is still pending |
| `FIX` | Prior review findings have been addressed and the issue is ready for re-review |
| `RECHECK` | No code change expected; reviewer should verify updated evidence, runtime result, or issue-thread clarification |
| `BLOCKED` | Cannot proceed without input, permission, missing dependency, or failed prerequisite |

---

## Security/Storage Handoff Checklist

For issues that modify encryption, storage, or security-critical paths, the handoff must include a **threat-path checklist**:
- [ ] Read path: what happens on success, wrong key, missing file, corruption?
- [ ] Write path: does it fail-closed on corruption? Can it overwrite unreadable data?
- [ ] Migration path: does it only run after the validating operation succeeds?
- [ ] Cache/memory path: when is sensitive data cleared? (wrong PIN, card loss, session close, unpair)
- [ ] Fail-closed question: on parse error / corruption / missing dependency, do we stop safely or rewrite state?

## API Change Handoff Checklist

For issues that change method signatures or state contracts:
- [ ] Caller audit: grep all callers (QML, C++, tests) and verify they use the new signature
- [ ] Expected visible behavior change: name what the user or consuming module sees differently, not just the code diff

## Default Verification Matrix

Unless the issue says otherwise, verify:
1. `nix develop` dev build (`cmake -B build -G Ninja && cmake --build build`)
2. `nix build .#lib`
3. `cd build && ctest --output-on-failure`

Host runtime validation in Basecamp is required **only** when the issue explicitly calls for real host-app behavior.

---

## Branch Naming

- `feature/description` — new features
- `security/pX-fixes` — security fixes by priority
- Delete merged branches locally and remotely after merge

---

## GitHub Issue Hygiene

**Labels**: every issue gets one type + one env label
- Type: `bug`, `feature`, `security`
- Env: `env:logos-basecamp`, `env:standalone`, `env:both`

**Bug body**: Symptom, Steps to reproduce, Expected, Actual, Suspected cause.

**Closing**: only when fix is merged to master. Use `Closes #N` in commit to auto-close.

---

## Logos Basecamp Updates Routine

Check weekly:
```bash
cd ~/logos-app && git fetch && git log HEAD..origin/master --oneline
```

If updates exist:
```bash
git pull && nix build '.#bin-appimage'
```

Re-run full UI/UX checklist after every Logos Basecamp update. Watch specifically for:
- `PluginInterface` changes — update `NotesPlugin` immediately if broken
- `LogosQmlBridge` new methods or behavior changes
- `logos-design-system` token changes affecting hardcoded colors

---

## Guiding Principle

Prefer simple and correct over clever and broken.

---

## Reference Locations

| What | Where |
|------|-------|
| Shell integration patterns | `~/logos-app/src/` (Logos Basecamp repo) |
| Seed phrase UX patterns | `~/status-desktop/ui/imports/shared/popups/addaccount/` |
| Logos design system | `~/Qt/6.9.3/gcc_64/` (installed) |
| Plugin QML | `plugins/notes_ui/Main.qml` |
| Backend core | `src/core/NotesBackend.cpp` |
| Plugin bridge | `src/plugin/NotesPlugin.cpp` |
| Crypto | `src/core/CryptoManager.cpp` |
| Tests | `tests/test_multi_note.cpp`, `tests/test_security.cpp` |
| Blog posts | `blog/` |
| Security review | `SECURITY_REVIEW.md` |
| Review loop script | `scripts/security-review-loop.sh` |
| Architecture reference | `docs/skills/architecture.md` |
| Lessons learned | `docs/skills/lessons.md` |
| Ecosystem & tools | `docs/skills/ecosystem.md` |
