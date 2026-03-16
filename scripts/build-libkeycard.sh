#!/usr/bin/env bash
# Build libkeycard.so from status-keycard-go (CGO shared library)
# Requires: Go toolchain, libpcsclite-dev
#
# Usage: ./scripts/build-libkeycard.sh
#
# If using Nix:
#   nix shell nixpkgs#go nixpkgs#pcsclite.dev nixpkgs#pkg-config --command ./scripts/build-libkeycard.sh

set -euo pipefail

KEYCARD_GO_DIR="${KEYCARD_GO_DIR:-$HOME/status-desktop/vendor/status-keycard-go}"
OUT_DIR="$(cd "$(dirname "$0")/.." && pwd)/lib/keycard"

if [ ! -d "$KEYCARD_GO_DIR/shared" ]; then
    echo "Error: status-keycard-go not found at $KEYCARD_GO_DIR"
    echo "Set KEYCARD_GO_DIR to the correct path"
    exit 1
fi

echo "Building libkeycard.so from $KEYCARD_GO_DIR..."
cd "$KEYCARD_GO_DIR/shared"

export CGO_ENABLED=1
go build -buildmode=c-shared -o "$OUT_DIR/libkeycard.so" .

echo "Built: $OUT_DIR/libkeycard.so ($(du -h "$OUT_DIR/libkeycard.so" | cut -f1))"
echo "Header: $OUT_DIR/libkeycard.h"

# Copy header if it was regenerated
if [ -f "$KEYCARD_GO_DIR/build/libkeycard/libkeycard.h" ]; then
    cp "$KEYCARD_GO_DIR/build/libkeycard/libkeycard.h" "$OUT_DIR/libkeycard.h"
fi
