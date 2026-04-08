# Building libkeycard from Source: Issue #44 Deep Dive

**Date:** 2026-03-19
**Author:** Alisher (with Claude Opus 4.6)
**Issue:** [#44](https://github.com/xAlisher/logos-notes/issues/44)

## The Problem

Logos Notes integrates smart card support through Status Keycard, enabling hardware-backed key storage for encrypted notes. Our initial implementation bundled a pre-built `libkeycard.so` binary extracted from the status-desktop build. This worked, but violated a core principle: **build everything from source**.

Pre-built binaries are problematic:
- No reproducibility guarantees
- Version tracking is manual and error-prone
- Dependency management is opaque
- Trust assumptions about the build environment

Issue #44 aimed to fix this by building `libkeycard.so` from the upstream `status-keycard-go` repository directly in our Nix flake.

## The Solution: Nix buildGoModule for CGO Shared Libraries

The [status-keycard-go](https://github.com/status-im/status-keycard-go) repository provides a Go-based implementation of the Keycard protocol. The `shared/` directory contains CGO bindings that compile to a C shared library.

We used Nix's `buildGoModule` to build this CGO shared library:

```nix
libkeycard = pkgs.buildGoModule {
  pname = "libkeycard";
  version = "unstable-2024-03-31";

  src = pkgs.fetchFromGitHub {
    owner = "status-im";
    repo = "status-keycard-go";
    rev = "76c880480c62dbf0ee67ee342f87ab80a928ed73";
    hash = "sha256-AcTMJm7aGSuh0emH+3Vun/BOdtC7ntwQVbakbKkrbFA=";
  };

  vendorHash = null;
  buildInputs = [ pkgs.pcsclite ];
  nativeBuildInputs = [ pkgs.pkg-config ];

  buildPhase = ''
    cd shared
    export CGO_ENABLED=1
    go build -buildmode=c-shared -o libkeycard.so .
  '';

  installPhase = ''
    mkdir -p $out/lib
    cp libkeycard.so $out/lib/
    cp libkeycard.h $out/lib/
  '';
};
```

Key details:
- `vendorHash = null`: No vendoring needed for this simple CGO build
- `buildInputs = [ pkgs.pcsclite ]`: Smart card communication requires PC/SC
- `CGO_ENABLED=1 go build -buildmode=c-shared`: Produces a C-compatible shared library

This gives us:
- Reproducible builds pinned to a specific upstream commit
- Transparent dependency management via Nix
- Easy version upgrades by changing the `rev` and `hash`

## The Challenge: Portable Bundling and System Services

With `libkeycard.so` built from source, we packaged it into an LGX module using the [nix-bundle-lgx](https://github.com/logos-co/nix-bundle-lgx) portable bundler:

```bash
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib
```

The `#portable` bundler creates truly portable artifacts by including **all** transitive dependencies, even system libraries. This ensures the LGX works across different Linux distributions without assuming available system libraries.

However, we hit a subtle issue: **smart card detection failed after installation**.

### The libpcsclite Socket Problem

The portable bundler automatically included `libpcsclite.so.1` (a dependency of `libkeycard.so`). On the surface, this seems correct — bundle everything for portability.

But `libpcsclite` is not a regular library. It's a **client library for the pcscd system daemon**, which manages smart card readers. Communication happens via a Unix domain socket, typically at `/run/pcscd/pcscd.comm`.

The bundled `libpcsclite.so.1` couldn't connect to this socket because:
1. It expected a different socket path (baked into the build)
2. The system's `pcscd` daemon wasn't visible from the bundled library's perspective

**Result:** Card readers were detected by the system (visible via `pcsc_scan`), but invisible to the Logos Notes module.

### The Solution: Post-Bundle Processing

The fix is simple but critical: **remove the bundled `libpcsclite.so.1`** so the system's `libpcsclite` (which knows the correct socket path) is used instead.

We created a single-step packaging command that handles this automatically:

```bash
#!/usr/bin/env bash
# scripts/package-lgx.sh
set -euo pipefail

OUTPUT_DIR="${1:-.}"
mkdir -p "$OUTPUT_DIR"

echo "==> Building core module LGX with portable bundler..."
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib

echo "==> Removing bundled libpcsclite for pcscd compatibility..."
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

tar -xzf logos-notes-core-lgx-1.0.0/logos-notes-core.lgx -C "$TEMP_DIR"
find "$TEMP_DIR" -name "libpcsclite.so*" -delete
(cd "$TEMP_DIR" && tar -czf "$OUTPUT_DIR/logos-notes-core.lgx" *)

echo "==> Building UI module LGX..."
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#ui
cp -L logos-notes-ui-lgx-1.0.0/logos-notes-ui.lgx "$OUTPUT_DIR/"

echo "✅ LGX packages ready in $OUTPUT_DIR"
```

Users run this via:

```bash
nix run .#package-lgx [output-dir]
```

This produces two working LGX files:
- `logos-notes-core.lgx` (with libpcsclite removed)
- `logos-notes-ui.lgx`

Both are ready for installation in the Logos App.

## Testing and Validation

We validated the solution through multiple test cycles:

1. **Fresh installs**: Removed all existing module installations, built LGX from scratch
2. **Ghost process cleanup**: Thoroughly killed all running Logos App instances (critical — multiple instances fighting over the same DB cause false failures)
3. **Smart card detection**: Verified card reader detection via the keycard module
4. **UI completeness**: Confirmed all icons (Add, Lock, close) render correctly

The cleanup command that worked reliably:

```bash
pkill -9 -f "LogosApp.elf"; pkill -9 -f "logos_host.elf"
```

(AppImage wraps processes via `ld-linux`, so you must match the `.elf` names with the `-f` flag.)

## Lessons Learned

### 1. Portable Bundling Has Limits

Bundling *everything* works for pure computation libraries, but fails for libraries that interact with system services via:
- Unix domain sockets (pcscd, dbus, etc.)
- System configuration files (/etc/*)
- Hardware device nodes (/dev/*)

For these, the system library must be used.

### 2. Build from Source > Pre-built Binaries

Despite the libpcsclite complication, building from source is the right choice:
- Reproducibility: pinned to `sha256-AcTMJm7aGSuh0emH+3Vun/BOdtC7ntwQVbakbKkrbFA=`
- Auditability: `nix build .#libkeycard` shows the exact build process
- Maintainability: upstream version updates = change one `rev` hash

### 3. Single-Step Commands Reduce Error

Our first iteration required two manual steps:
1. `nix bundle --bundler ... .#lib`
2. `nix run .#fix-lgx logos-notes-core.lgx`

This presented users with a broken intermediate artifact. The `package-lgx` script combines both steps into one atomic operation.

### 4. Test with Fresh State

Multiple test rounds failed because we didn't aggressively clean ghost processes and old module installations. **Always start fresh** when testing Logos App modules:
- Kill all running instances
- Remove existing module installations if testing packaging changes
- Verify the LGX contents with `tar -tzf` before installing

## Conclusion

Issue #44 took us from a brittle pre-built binary to a fully reproducible build-from-source workflow. The journey exposed a subtle constraint of portable bundling for system-service-dependent libraries, which we solved with automated post-processing.

The result: `nix run .#package-lgx` produces shippable LGX artifacts in one command, no manual steps required.

### Try It Yourself

```bash
# Clone and enter the dev environment
git clone https://github.com/xAlisher/logos-notes.git
cd logos-notes
nix develop

# Build LGX packages
nix run .#package-lgx /tmp/lgx-output

# Inspect the core LGX
tar -tzf /tmp/lgx-output/logos-notes-core.lgx | grep -E "(libkeycard|libpcsclite)"
# Should show libkeycard.so but NOT libpcsclite.so*

# Install in Logos App
lgpm install /tmp/lgx-output/logos-notes-core.lgx
lgpm install /tmp/lgx-output/logos-notes-ui.lgx
```

---

**Further Reading:**
- [Logos Notes PROJECT_KNOWLEDGE.md](../../PROJECT_KNOWLEDGE.md)
- [status-keycard-go](https://github.com/status-im/status-keycard-go)
- [nix-bundle-lgx](https://github.com/logos-co/nix-bundle-lgx)
- [PC/SC Lite Project](https://pcsclite.apdu.fr/)
