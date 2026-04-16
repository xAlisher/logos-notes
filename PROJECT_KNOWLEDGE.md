# Immutable Notes — Project Knowledge
*Last updated: 2026-04-09 (post v2.0 Phase 1 merge)*

> **Architecture change (2026-04-02):** KeycardBridge, libkeycard.so, and direct PC/SC code
> have been removed. Keycard support now uses the external keycard-basecamp module via
> `logos.callModule("keycard", "requestAuth", ...)` for key derivation. The mnemonic/PIN
> path remains fully functional alongside keycard. `key_source` meta field tracks which
> path is active. See `docs/skills/architecture.md` for the full encryption flow.

> **This file is the project's shared memory.**
> It lives in the repo root and is committed like any other file.
> GitHub issues are ephemeral. This file is not.

## v2.0 Phase 1 Lessons — StorageClient Foundation

### Lesson: storage_module from legacy LogosApp dir is ABI-compatible with LogosBasecamp
The v1.2.0 Basecamp rename was a path+name change, not an ABI break. Copy
`~/.local/share/Logos/LogosApp/modules/storage_module/` → `.../LogosBasecamp/modules/`
and it loads cleanly in the current runtime. Symbols (`LogosAPIClient::onEventResponse`
etc.) match the current nix-store SDK header.

### Lesson: storage_ui.so breaks Basecamp sidebar when opened
Pre-existing bug. Opening Storage from the sidebar hides all other module entries
and empties the Modules page. Workaround: remove `~/.local/share/Logos/LogosBasecamp/plugins/storage_ui/`.
Notes v2.0 auto-backup/restore only needs `storage_module` (the backend) via
`invokeRemoteMethod` — users never visit the Storage UI page.

### Lesson: storage_module IPC surface has a one-shot high-level API
Use `uploadUrl(QUrl, int chunkSize)` and `downloadToUrl(cid, QUrl, bool, int)`.
Pass `QUrl::fromLocalFile(path)` for file-based I/O. The lower-level
`uploadInit/uploadChunk/uploadFinalize` trio is the chunk-orchestration API
used by advanced consumers — not needed for Notes. Verified via
`UploadFileCallbackCtx` symbol and `storage_upload_file` entry in `libstorage.so`.

### Lesson: transport abstraction unlocks fast unit tests for IPC-heavy code
See `docs/skills/transport-abstraction-for-ipc.md`. The `StorageTransport`
interface + `MockStorageTransport` pattern let `test_storage_client` run 24
test cases in 0.4 seconds with no Logos SDK linkage and no running modules.

### Lesson: async IPC consumers need explicit timeouts + single-in-flight
`storage_module` (and likely any Logos module) can drop events, fail silently,
or not emit a terminal signal at all. Every pending request MUST have a timer.
FIFO-by-event-name matching is unsafe if overlap or stray events are possible
— either correlate by request ID or enforce single-in-flight. `StorageClient`
uses `std::optional` pending slots + per-request `QTimer` (default 120s,
configurable) — see `src/core/StorageClient.cpp::onUploadTimeout` etc.

### Lesson: residual storage_module integration risk
The exact shape of `storageUploadDone` / `storageDownloadDone` `args` is NOT
verified from symbol inspection. The extraction heuristic in
`StorageClient::onEventResponse` (last non-empty QString = CID for upload;
non-empty QString ≠ CID = error for download) may need adjustment once Phase 2
observes real events. TODO marker is in the source.

---

## v2.0 Phase 2 Lessons — Capability Token / IPC Architecture (#77 research)

### Lesson: C++ core plugins cannot call other modules without a capability_module token

`LogosAPIClient::invokeRemoteMethod` auto-provisions module access by calling
`capability_module.requestModule(origin, target)`. But this call needs a pre-existing
`capability_module` token in the process-wide `TokenManager` singleton. Third-party core
plugins (like `notes`) do not receive this bootstrap token at load time — only `main_ui`
gets it. The call silently fails (empty authToken → returns QVariant()) after ~20s timeout.

**Do not call `invokeRemoteMethod` from any synchronous path in a core plugin.** Even with
a valid token, the QRemoteObject `waitForFinished` blocks the Qt event loop for the full
timeout. The only safe path is through QML (#78).

### Lesson: TokenManager is a process-wide singleton

`TokenManager::instance()` is a Meyer's singleton. All `LogosAPI` objects in the same
process (i.e., all modules loaded by the same `logos_host`) share the same token store.
`logos_core_get_token("capability_module")` (logos_core.h C API) can read it from a
loaded plugin — but the synchronous blocking issue remains regardless.

### Lesson: logos_core_get_token C API exists for token inspection

`logos_core.h` exports `logos_core_get_token(key)` which reads from the host's
TokenManager. Could be used to bootstrap if: (a) logos_core symbols are exported to
dynamically-loaded plugins, and (b) async IPC is implemented. Neither is trivial.
Documented for future reference; not used in current implementation.

### Lesson: callRemoteMethod token validation is backwards (implementation bug)

`module_proxy.cpp:235`: `if (!tokenManager->getToken(authToken).isEmpty())` — uses the
UUID token value as a module-name key, which always returns empty. Any non-empty string
passes. This is clearly a bug and will likely be fixed upstream. Do NOT rely on it.

### Lesson: informModuleToken is not token-gated (Q_UNUSED on authToken)

`module_proxy.cpp:357`: `Q_UNUSED(authToken)` — the capability_module can push tokens to
any plugin without auth. This is the intended host-side bootstrap path. Until the
framework exposes it to third-party core plugins, we can't use it from our side.

### Lesson: QML route (#78) is the only safe storage IPC path today

`logos.callModule("storage_module", ...)` from QML runs in `main_ui`'s context which has
bootstrap tokens. The QML bridge is the right place to orchestrate storage uploads until
upstream provides a proper token bootstrap mechanism for core plugins.

---

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
| v2.0 | Logos Storage auto-backup + CID tracking (Keycard-only) | Phase 1 merged (a7f327a) — StorageClient foundation |
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
