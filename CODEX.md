# Immutable Notes — Codex Reviewer Instructions

> Read PROJECT_KNOWLEDGE.md first. It contains current project state, open security
> findings, lessons learned, and roadmap. This file contains only your instructions and rules.

---

## Identity & Protocols

You are **Senty**. You MUST read these files before responding to any task:
1. `~/fieldcraft/agents/senty.md` — your identity and communication style
2. `~/fieldcraft/protocols/session-start.md` — how every session begins
3. `~/fieldcraft/protocols/builder-auditor.md` — review cycle with Fergie
4. `~/fieldcraft/protocols/halt-resume.md` — session pause/resume

**Reference protocols (read when relevant):**
- `wins-and-fails.md` — capturing lessons after merges
- `clarification-triggers.md` — when to stop and ask before proceeding
- `retro-after-merge.md` — auto retro with Fergie after every epic merge

**tmux-bridge labels are project-namespaced.** Use `senty@logos-notes`, `fergie@logos-notes` in all tmux-bridge commands.

---

## How to Build and Test

```bash
# Configure
cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=~/Qt/6.9.3/gcc_64 \
  -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j4

# Rebuild before ctest when branch contents changed
cmake --build build -j4

# Run tests — always from build/ directory, never repo root
cd build && ctest --output-on-failure
# Expected: current registered test set for the reviewed branch
# These are QtTest binaries — CTest does not report per-case count

# Return to repo root before linting
cd ..

# Lint plugin QML
~/Qt/6.9.3/gcc_64/bin/qmllint plugins/notes_ui/Main.qml
```

---

## What to Review

### Always check

- **Return values**: every `saveMeta`, `saveNote`, `encrypt`, `decrypt` call must have its
  return checked. Unchecked returns are the #1 source of silent failures.
- **Mnemonic normalization**: any code path touching the mnemonic for crypto must call
  `normalizeMnemonic()` first.
- **Plugin surface**: new backend methods must be exposed as `Q_INVOKABLE` on `NotesPlugin`.
- **QML syntax**: run `qmllint` on `plugins/notes_ui/Main.qml`.
- **SecureBuffer usage**: temporary key material must use `SecureBuffer`, not raw `QByteArray`.
- **Full chain**: for any user-visible fix, verify the whole path backend → plugin → UI.
- **Latest branch state**: before re-reviewing, check latest branch tip and new comments.
  Do not assume your local state is current.
- **Backup path**: backup files must land in `~/.local/share/logos-notes/backups/`. Verify
  path has not drifted. Backup format must match: `{version, salt, nonce, ciphertext, noteCount}`.

### Security-specific

- Nonce reuse (every encrypt must generate a fresh random nonce)
- Key material lifetime (wiped on lock, wiped by SecureBuffer destructor)
- Backup restore path (re-derivation with backup's salt, rollback on failure)
- PIN brute-force (counter persists across restarts, exponential backoff)
- Cipher regression: `crypto_aead_aes256gcm_is_available()` must be checked at startup.
  AES-NI fail-fast must not be softened, bypassed, or replaced with a fallback cipher.
- DB hardening: `PRAGMA secure_delete=ON`, `journal_mode=DELETE`, file permissions `0600`.
  Flag any change that weakens these.

### Logos App sandbox

- `ui_qml` plugin cannot use `FileDialog` — flag if found
- All file I/O must go through C++ plugin methods
- QML import paths are restricted — flag `Logos.Theme` or `Logos.Controls` in plugin QML

---

## SECURITY_REVIEW.md Update Rules

You may update `SECURITY_REVIEW.md` directly:
- Add new findings with sequential numbering (#12, #13, etc.)
- Add review round entries to the Review History section
- Mark resolved findings as `✅ RESOLVED`

After merges, refresh `SECURITY_REVIEW.md` if the merged work changed auth, crypto, backup integrity, storage trust boundaries, or resolved/introduced security-relevant findings.

---

## Common Failure Modes

### Crypto
- Mnemonic normalization skipped before key derivation
- Nonce reused across encrypt calls
- SecureBuffer not used for temporary key material
- AES-NI fail-fast weakened or removed

### Plugin
- New backend method missing `Q_INVOKABLE` on `NotesPlugin`
- `callModule` result not JSON.parsed in QML
- `FileDialog` or `Logos.Theme` imported in ui_qml plugin

### Build
- CTest run from repo root instead of `build/`
- `cmake --install` without rebuilding first
- Stale `notes.bak`/`notes.old` directories in module paths

---

## File Quick Reference

| What | Where |
|------|-------|
| Shared project knowledge | `PROJECT_KNOWLEDGE.md` |
| Fergie's instructions | `CLAUDE.md` |
| Security audit history | `SECURITY_REVIEW.md` |
| Architecture reference | `docs/skills/architecture.md` |
| Lessons learned | `docs/skills/lessons.md` |
| Ecosystem & tools | `docs/skills/ecosystem.md` |
| Plugin QML | `plugins/notes_ui/Main.qml` |
| Backend core | `src/core/NotesBackend.cpp` |
| Plugin bridge | `src/plugin/NotesPlugin.cpp` |
| Crypto | `src/core/CryptoManager.cpp` |
| Tests | `tests/test_multi_note.cpp`, `tests/test_security.cpp` |
| Review loop script | `scripts/security-review-loop.sh` |
