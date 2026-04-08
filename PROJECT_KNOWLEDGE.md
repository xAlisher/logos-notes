# Immutable Notes — Project Knowledge
*Last updated: 2026-04-09*

> **Architecture change (2026-04-02):** KeycardBridge, libkeycard.so, and direct PC/SC code
> have been removed. Keycard support now uses the external keycard-basecamp module via
> `logos.callModule("keycard", "requestAuth", ...)` for key derivation. The mnemonic/PIN
> path remains fully functional alongside keycard. `key_source` meta field tracks which
> path is active. See `docs/skills/architecture.md` for the full encryption flow.

> **This file is the project's shared memory.**
> It lives in the repo root and is committed like any other file.
> GitHub issues are ephemeral. This file is not.

## Epic #62 Lessons — Keycard Module Integration

### Lesson: card secure channel state persists across sessions
After `authorizeRequest` completes and session auto-closes, the card's secure channel is stale. The next `openSecureChannel` fails. Fix: retry with fresh `select()`.

### Lesson: bridge state names must match QML state checks
`getState()` returns `"AUTHORIZED"` after auth, but QML only checked `"CARD_PRESENT"`, `"READY"`, `"SESSION_ACTIVE"`. Must include all card-present states.

### Lesson: always check backend response shape before writing QML
`listBackups()` returns raw array but QML expected `{backups:[...]}`. Always inspect actual response before writing the parser.

### Lesson: rewrite large QML files from scratch instead of patching
Patching 50+ references in 1500-line QML was error-prone. Rewriting from scratch (850 lines) was faster and cleaner.

### Lesson: install paths differ between repos — verify before testing
logos-notes installs to `LogosBasecampDev/`, keycard-basecamp to `LogosBasecamp/`. Basecamp loads from `LogosBasecamp/`. Must copy or adjust paths.

---

## What We're Building

Encrypted, local-first Markdown notes app for the Logos ecosystem.

- **Approved idea**: https://github.com/logos-co/ideas/issues/13
- **Repo**: https://github.com/xAlisher/logos-notes
- **Stack**: C++17, Qt 6.9.3, QML, libsodium 1.0.18, SQLite, CMake + Nix
- **OS**: Ubuntu 24.04

### Vision
Encrypted notes with Keycard hardware key protection, synced across devices via Logos Messaging and Logos Storage. No accounts. No servers. Your recovery phrase is your identity.

---

## Current Phase: v1.2.0 released — Logos Basecamp compatible

### Completed Phases

| Version | What shipped |
|---------|-------------|
| v0.1.0 | Phase 0 — single note, module registration |
| v0.2.0 | Phase 1 — multiple notes, sidebar |
| v0.3.0 | Security hardening (P0+P1) |
| v0.4.0 | P2 security fixes, AES-NI fail-fast |
| v0.5.0 | Settings, backup export/import, stable identity |
| v0.6.0 | LGX packaging, 95-case test suite (backup, account, note API, plugin) |
| v1.0.0 | Keycard hardware key derivation + UI polish |
| v1.1.0 | Build libkeycard from source + portable LGX packaging (issue #44) |
| v1.2.0 | Logos Basecamp compatibility (app rename, path updates) |

### Roadmap

| Version | Description | Status |
|---------|-------------|--------|
| v0.6.0 | LGX package for Logos App Package Manager | ✅ Complete |
| v0.6.0 | AppImage standalone installer | Parked — blocked on Qt QML AOT |
| v1.0.0 | Keycard hardware key derivation + UI polish | ✅ Complete |
| v1.1.0 | Build libkeycard from source + portable LGX packaging | ✅ Released |
| v1.2.0 | Logos Basecamp compatibility | ✅ Released — issue #55 complete |
| v2.0 | Logos Storage auto-backup + CID tracking | Research |
| v3.0 | Trust Network — social backup via web of trust | Proposal stage |

---

## Open Security Findings

| # | Severity | Finding | Status |
|---|----------|---------|--------|
| #10 | Low | PIN lockout counter in same DB as wrapped key. Offline attacker can reset counter between guesses. Primary defense is Argon2id cost (~0.7s/guess). Keycard (v1.0.0) eliminates this. | Open — accepted, documented |
| #8 | Low | AAD domain separation not implemented. AEAD calls don't bind note ID or schema version. | Open — future hardening |

---

## Open Questions

1. **New Logos App repo**: ✅ Resolved. `logos-co/logos-app` (now "Logos Basecamp") is the official successor. The repo is active, receiving updates, and introduced dev/portable build discrimination. `logos-app-poc` is archived/legacy.
2. **initLogos signature**: ✅ Resolved. `initLogos(LogosAPI*)` is correct. Called reflectively via `QMetaObject::invokeMethod` (see lesson #19).
3. **Social backup CID discovery**: ✅ Resolved with depth. Giuliano (2026-03-17) provided detailed guidance:
   - **Permissioned groups required** — can't be permissionless or you get abuse. Trust group members by construction.
   - **Rolling updates** (new blob replaces old) looks like Status' historical message archives. No built-in "latest CID" lookup.
   - **CID delivery options**: (a) Waku messaging with MVDS/SDS for reliable delay-tolerant delivery, or (b) request/response protocol on join — broadcast ask, get CID replies. Not expensive for small groups. (c) On-chain CID placement if you don't mind gas (gasless chain available).
   - **No remote download requests** on storage nodes currently — no security model. Permissioned groups assume members are incentivized to store by construction and trust each other not to abuse.
   - **Blob size**: make blobs as large as possible for efficient network use. Batch all notes into one encrypted blob per backup, not per-note CIDs.
   - **Post-mainnet**: incentivized storage with provider protections is planned but not available yet.
   - **Decision**: permissioned trust groups via Waku messaging. Request/response CID exchange on group join. One encrypted blob per backup.
4. **Logos Storage built-in encryption**: bkomuves mentioned automatic encryption may be built-in. Not yet implemented. Worth watching before Phase 2 design.
5. **AppImage unblock**: three options identified — `QT_QML_NO_CACHEGEN=1`, `qt_deploy_qml_imports()`, or Nix bundle. Option 3 recommended. Not yet attempted.

---

## Parked Tasks

### AppImage packaging
Blocked on Qt 6.9 AOT-compiled QML type registration conflict with external plugin paths.
Upstream issue: https://github.com/logos-co/logos-app/issues/60

Unblock options (in priority order):
1. `nix bundle --bundler github:logos-co/nix-bundle-lgx` (recommended)
2. `qt_deploy_qml_imports()` CMake function
3. `QT_QML_NO_CACHEGEN=1` (interpreted mode, simplest)

---

## Reference Files

| What | Where |
|------|-------|
| Architecture, encryption, schema, plugin contract | `docs/skills/architecture.md` |
| Lessons learned (38 entries) | `docs/skills/lessons.md` |
| Ecosystem tools, storage research, links | `docs/skills/ecosystem.md` |
| Security audit history | `SECURITY_REVIEW.md` |
| Decision records | `docs/decisions/` |
| Retro log | `docs/retro-log.md` |
