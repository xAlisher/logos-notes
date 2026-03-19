#!/usr/bin/env bash
# Canonical LGX packaging command - produces working artifacts in one step
set -euo pipefail

echo "==> Building core module LGX with portable bundler..."
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib

echo "==> Removing bundled libpcsclite for pcscd compatibility..."
LGX_CORE="logos-notes-core-lgx-1.0.0/logos-notes-core.lgx"
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

tar -xzf "$LGX_CORE" -C "$TEMP_DIR"
find "$TEMP_DIR" -name "libpcsclite.so*" -delete
(cd "$TEMP_DIR" && tar -czf logos-notes-core.lgx *)

# Replace with fixed version
cp "$TEMP_DIR/logos-notes-core.lgx" "$LGX_CORE"

echo "==> Building UI module LGX..."
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#ui

echo ""
echo "✅ LGX packages ready:"
echo "   - logos-notes-core-lgx-1.0.0/logos-notes-core.lgx (fixed, pcscd-compatible)"
echo "   - logos-notes-ui-lgx-1.0.0/logos-notes-ui.lgx"
