#!/usr/bin/env bash
set -euo pipefail

# Security review loop — sends git diff to Codex for review
# Usage: ./scripts/security-review-loop.sh [commit_range] [file_pattern]
# Example: ./scripts/security-review-loop.sh HEAD~1 "src/core/"

COMMIT_RANGE=${1:-HEAD~1}
FILE_PATTERN=${2:-"src/core/"}

# Temp files in /tmp to avoid Codex home-dir permission issues
DIFF_FILE=$(mktemp /tmp/security-review-diff.XXXXXX)
PROMPT_FILE=$(mktemp /tmp/security-review-prompt.XXXXXX)

cleanup() {
  rm -f "$DIFF_FILE" "$PROMPT_FILE"
}
trap cleanup EXIT

echo "=== Generating diff for review ==="
git diff "$COMMIT_RANGE" HEAD -- "$FILE_PATTERN" > "$DIFF_FILE"

if [ ! -s "$DIFF_FILE" ]; then
  echo "No changes found for pattern: $FILE_PATTERN"
  exit 1
fi

LINES=$(wc -l < "$DIFF_FILE")
echo "=== Diff size: $LINES lines ==="
echo ""

cat > "$PROMPT_FILE" <<EOF
You are a security-focused code reviewer specializing in
cryptography and C++. Review this git diff for:
1. Cryptographic correctness
2. Memory safety and key material handling
3. Input validation
4. Any regressions or new vulnerabilities introduced

Be specific. Reference line numbers. Flag anything suspicious.

Diff:
$(cat "$DIFF_FILE")
EOF

echo "=== Sending to Codex for security review ==="
if ! codex < "$PROMPT_FILE"; then
  EXIT_CODE=$?
  echo ""
  echo "ERROR: codex exited with code $EXIT_CODE"
  echo "Diff saved at: $DIFF_FILE"
  echo "Prompt saved at: $PROMPT_FILE"
  # Keep temp files on failure for debugging
  trap - EXIT
  exit $EXIT_CODE
fi

echo ""
echo "=== Review complete. Paste findings to Claude Code to address. ==="
