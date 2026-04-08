# Ecosystem Reference

> Reference for cross-project work, storage integration, and external dependencies.

---

## Logos Developer Tools

| Tool | Purpose |
|------|---------|
| `lm` | Inspect compiled module binaries (metadata + method signatures) |
| `logoscore` | Headless runtime — load modules and invoke methods from CLI without logos-app |
| `lgpm` | Package manager CLI — install/list LGX packages |
| `logos-cpp-generator` | Generate typed SDK wrappers from compiled modules |
| `mkLogosModule` | Nix function — builds core modules from `module.yaml` |

---

## Logos Storage Research Notes

### Architecture
```
libstorage.so (Nim, ~14MB) — Logos Storage node (formerly Codex)
storage_module_plugin.so — Qt C++ wrapper
storage_ui.so — QML UI plugin
```

### Network Ports
| Port | Protocol | Service |
|------|----------|---------|
| 8500 | TCP | libp2p transport |
| 8090 | UDP | DiscoveryV5 peer discovery |
| 8080 | TCP | REST API |

### REST API
```
POST /api/storage/v1/data          — upload file → CID
GET  /api/storage/v1/data/{cid}    — download
POST /api/storage/v1/data/{cid}/network — fetch from network
GET  /api/storage/v1/debug/info    — node status
```

### Key Findings (tested 2026-03-12)
- Upload → CID ✅, fetch while online ✅
- Blobs stored locally only — no auto-replication to peers
- Delete = gone permanently if no other node has fetched it
- Single device limitation: full sync requires minimum 2 nodes
- Works on cellular with `nat: "none"` in config
- **Storage team confirmed**: web-of-trust peer storage is intended usage

### Integration Path for Phase 2
- Notes module calls storage_module via `LogosAPIClient::invokeRemoteMethod()`
- Upload: `uploadInit(filepath, chunkSize)` → `uploadChunk()` → `uploadFinalize()` → CID
- Download: `downloadToUrl(cid, destUrl, local, chunkSize)` → file at destination
- All operations async via `eventResponse` signal

---

## Import Path Vision (v1.0.0+)

Three ways to derive the same 256-bit AES-256-GCM master key:

```
Import Screen:
  ┌─ 1. Enter Recovery Phrase ──→ Argon2id → master key (current, always available)
  ├─ 2. Connect Keycard ────────→ card derives m/43'/60'/1581' → master key (#33)
  └─ 3. Connect Logos Wallet ───→ accounts_module derives key → master key (#32)
```

Same DB, same encryption, same notes. User picks their preferred key management.
Switching between methods: re-encrypt via backup/restore flow.
Keycard (#33) and wallet (#32) are independent features — neither blocks the other.

### Keycard Integration (v1.0.0)

- Hardware: Status Keycard (ISO 7816) + USB PC/SC reader
- Library: `status-keycard-go` compiled as `libkeycard.so`, thin C++ wrapper (`KeycardBridge`)
- Key derivation: BIP44 path `m/43'/60'/1581'` (EIP-1581 encryption root)
- Note: Keycard uses secp256k1, current fingerprint uses Ed25519 — need domain separation
- Phase 1: link into notes_plugin directly. Phase 2: extract shared keycard-module for ecosystem.
- Reference: `~/status-desktop/vendor/status-keycard-go/` and `vendor/status-keycard-qt/`
- Build: `scripts/build-libkeycard.sh` compiles Go → `lib/keycard/libkeycard.so`
- C API: `KeycardInitializeRPC()`, `KeycardCallRPC()`, `KeycardSetSignalEventCallback()`, `Free()`
- JSON-RPC methods: `keycard.Start`, `keycard.Stop`, `keycard.GetStatus`, `keycard.Authorize`, `keycard.ExportRecoverKeys`

#### Issue #44: Build libkeycard from source (2026-03-19)

**Problem**: Pre-built `libkeycard.so` binary committed to repo (14MB, commit 76c8804)

**Solution**: Build from source in Nix flake using `pkgs.buildGoModule`:
```nix
libkeycard = pkgs.buildGoModule {
  src = pkgs.fetchFromGitHub {
    owner = "status-im";
    repo = "status-keycard-go";
    rev = "76c880480c62dbf0ee67ee342f87ab80a928ed73";
  };
  buildPhase = ''
    cd shared
    export CGO_ENABLED=1
    go build -buildmode=c-shared -o libkeycard.so .
  '';
};
```

**Backup strategy**: Keep committed binary at `lib/keycard/libkeycard.so` as fallback for several months until Nix builds proven stable.

**LGX Packaging findings**:
1. **Platform mismatch**: Default bundler generates `linux-amd64-dev`, Logos App expects `linux-amd64`
   - Solution: Use `nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib`
2. **libpcsclite issue**: Portable bundler includes `libpcsclite.so.1` (libkeycard dependency), but bundled version cannot connect to system pcscd daemon socket
   - Root cause: Bundled library looks for pcscd socket in wrong location
   - Workaround: Remove `libpcsclite.so.1` from `modules/notes/` after LGX installation to force system version
   - Documented in `flake.nix` with inline comment

**Testing**: ✅ Full LGX workflow validated with both core and UI modules, smart card detection working after libpcsclite workaround.

#### Sub-issue tracker
| # | Title | Branch | Status |
|---|-------|--------|--------|
| #34 | Reader detection + card state UI | merged to master | ✅ Complete — hardware verified, Codex LGTM |
| #35 | PIN authorization + key export | merged to master | ✅ Complete — Codex LGTM, hardware verified |
| #36 | Wire key into NotesBackend encryption | merged to master | ✅ Complete — same merge as #35 |
| #37 | Keycard ↔ mnemonic migration path | — | Postponed — not needed for v1.0.0 |
| #44 | Build libkeycard from source + LGX packaging | feature/shared-keycard-module | ✅ Complete — ready for merge after review |

### Wallet Integration (v0.7.0+)

- `accounts_module` wraps `go-wallet-sdk` — provides `createRandomMnemonic()`, HD keystore, PIN management
- Lightweight option: "Generate with Wallet" button on import screen (~10 lines QML)
- Full option: wallet holds the key, notes requests it via LogosAPI

---

## Phase 3 — Trust Network (Proposal)

**Concept**: small group of trusted friends store each other's encrypted backups. No central server.

**Flow**:
1. App auto-uploads encrypted backup to local Logos Storage node → CID
2. Settings → Trust Network: paste list of trusted public keys
3. App sends CID to trusted peers via Logos Messaging
4. Peer apps fetch + pin the blob if sender is in their trust list
5. Mutual by design — both sides must add each other

**Dashboard**:
```
Storing for me:    3 / 5 nodes online
I'm storing for:   4 identities
Last backup:       2 min ago
Backup CID:        zDvZ...  [Copy]
```

**Abuse prevention**: per-peer storage limit, revoke trust instantly, block key.

---

## Blog Posts

| File | Topic | Status |
|------|-------|--------|
| `blog/2026-03-12-phase-0.md` | Phase 0 — module, crypto architecture | Submitted to Logos press, pending publication |
| `blog/2026-03-14-security-hardening.md` | Security hardening, two-AI review loop | Published |
| `blog/2026-03-14-settings-backup-identity.md` | Settings, backup, identity/fingerprint | Published |
| `blog/2026-03-15-hotfixes-and-sandbox-lessons.md` | Hotfixes, QML sandbox lessons | Published |
| `blog/2026-03-15-shared-memory.md` | Shared memory, knowledge/instructions split, agent collaboration | Draft |
| `blog/2026-03-15-building-immutable-notes-on-logos.md` | Comprehensive Logos blog post — app, crypto, module architecture, lessons | Ready for Logos submission |

---

## Local Clone Paths

| Repo | Local path |
|------|-----------|
| logos-app (Logos Basecamp, built AppImage runs) | `~/logos-app/` |
| status-desktop (QML/Nim reference) | `~/status-desktop/` |

---

## Key Links

| Resource | URL |
|----------|-----|
| Ideas issue | https://github.com/logos-co/ideas/issues/13 |
| Project repo | https://github.com/xAlisher/logos-notes |
| Logos Basecamp (app) | https://github.com/logos-co/logos-app |
| Logos App PoC (legacy) | https://github.com/logos-co/logos-app-poc |
| Chat UI reference | https://github.com/logos-co/logos-chat-ui |
| Chat module reference | https://github.com/logos-co/logos-chat-module |
| Template module | https://github.com/logos-co/logos-template-module |
| Developer tutorial | https://github.com/logos-co/logos-tutorial |
| C++ SDK | https://github.com/logos-co/logos-cpp-sdk |
| Logos docs | https://github.com/logos-co/logos-docs |
| Logos Storage | https://github.com/logos-storage/logos-storage-nim |
| Logos Messaging | https://github.com/logos-messaging/nim-chat-poc |
| Waku docs | https://docs.waku.org |
| Design system | https://github.com/logos-co/logos-design-system |
| nix-bundle-lgx | https://github.com/logos-co/nix-bundle-lgx |
| Package Manager module | https://github.com/logos-co/logos-package-manager-module |
| Status contact | https://status.app/u/CwmAChEKD0FsaXNoZXIgU2hlcmFsaQM= |
