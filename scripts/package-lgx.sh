#!/usr/bin/env bash
# Canonical LGX packaging command - produces working artifacts in one step
set -euo pipefail

# Ensure nix is in PATH
export PATH="/nix/var/nix/profiles/default/bin:$PATH"

OUTPUT_DIR="${1:-.}"
mkdir -p "$OUTPUT_DIR"

# Convert to absolute path
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)

echo "==> Building core module LGX with portable bundler..."
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#lib

echo "==> Copying from nix store to output directory..."
cp -L logos-notes-core-lgx-1.0.0/logos-notes-core.lgx "$OUTPUT_DIR/logos-notes-core.lgx"

echo "==> Building UI module LGX..."
nix bundle --bundler github:logos-co/nix-bundle-lgx#portable .#ui

echo "==> Copying UI LGX to output directory..."
cp -L logos-notes-ui-lgx-1.0.0/logos-notes-ui.lgx "$OUTPUT_DIR/logos-notes-ui.lgx"

echo ""
echo "✅ LGX packages ready in $OUTPUT_DIR:"
echo "   - logos-notes-core.lgx"
echo "   - logos-notes-ui.lgx"
