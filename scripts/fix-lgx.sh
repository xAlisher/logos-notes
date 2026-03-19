#!/usr/bin/env bash
# Post-process LGX to remove libpcsclite.so.1 for pcscd compatibility
set -euo pipefail

LGX_FILE="${1:-logos-notes-core-lgx-1.0.0/logos-notes-core.lgx}"

if [[ ! -f "$LGX_FILE" ]]; then
  echo "Error: LGX file not found: $LGX_FILE"
  echo "Usage: $0 <path-to-lgx-file>"
  exit 1
fi

TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "Extracting $LGX_FILE..."
tar -xzf "$LGX_FILE" -C "$TEMP_DIR"

echo "Removing libpcsclite.so*..."
find "$TEMP_DIR" -name "libpcsclite.so*" -delete

echo "Repackaging..."
(cd "$TEMP_DIR" && tar -czf "$(basename "$LGX_FILE")" *)
mv "$TEMP_DIR/$(basename "$LGX_FILE")" "$LGX_FILE"

echo "✅ Fixed: $LGX_FILE (libpcsclite removed for system pcscd compatibility)"
