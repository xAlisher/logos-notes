#!/usr/bin/env bash
# Security review loop — pipes git diff to Codex for review
# Usage: ./scripts/security-review-loop.sh [commit_range] [file_pattern]
# Example: ./scripts/security-review-loop.sh HEAD~1 "src/core/"

COMMIT_RANGE=${1:-HEAD~1}
FILE_PATTERN=${2:-"src/core/"}

echo "=== Generating diff for review ==="
DIFF=$(git diff $COMMIT_RANGE HEAD -- $FILE_PATTERN)

if [ -z "$DIFF" ]; then
  echo "No changes found for pattern: $FILE_PATTERN"
  exit 1
fi

echo "=== Diff size: $(echo "$DIFF" | wc -l) lines ==="
echo ""

PROMPT="You are a security-focused code reviewer specializing in
cryptography and C++. Review this git diff for:
1. Cryptographic correctness
2. Memory safety and key material handling
3. Input validation
4. Any regressions or new vulnerabilities introduced

Be specific. Reference line numbers. Flag anything suspicious.

Diff:
$DIFF"

echo "=== Sending to Codex for security review ==="
echo "$PROMPT" | codex

echo ""
echo "=== Review complete. Paste findings to Claude Code to address. ==="
