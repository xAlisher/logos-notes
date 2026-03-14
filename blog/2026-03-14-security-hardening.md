# Security Hardening: Two AIs Reviewing Each Other's Work

After shipping Phase 0 and Phase 1 (multi-note CRUD), I spent a full session doing nothing but security work. The interesting part wasn't the fixes themselves — it was the process.

## The two-AI review loop

I used two AI coding tools against each other. Claude Code (Opus) wrote the fixes. OpenAI Codex reviewed the diffs. I sat in the middle, deciding what to act on.

The workflow:

1. Claude Code implements a security fix
2. I pipe the git diff to Codex via a review script
3. Codex reviews for crypto correctness, memory safety, input validation
4. I paste findings back to Claude Code
5. Claude Code fixes what Codex found
6. Repeat until clean

This caught real bugs. Not theoretical ones — actual regressions that would have shipped.

## What Codex caught

The most serious finding: Claude Code added XChaCha20-Poly1305 as a fallback cipher for CPUs without AES-NI hardware acceleration. Reasonable idea. But the cipher choice was detected at runtime and never persisted with the encrypted data.

If you encrypted notes with AES-256-GCM on a machine with AES-NI, then moved the database to a machine without it, the app would try to decrypt with XChaCha20. Your data would be unreadable. Not corrupted — just permanently inaccessible unless you moved it back to an AES-NI machine.

Codex flagged this as High severity. It was right.

## The XChaCha decision

The first instinct was to fix it properly — persist the cipher choice in the database, load it on startup, use the stored cipher for existing data. I implemented this. It worked.

Then I stepped back and asked: is this complexity worth it?

AES-NI has been standard on every x86_64 CPU since Intel Westmere in 2010. ARM has equivalent acceleration since ARMv8. In 2026, finding a CPU without hardware AES is like finding a computer without USB. It exists in theory but not in any scenario where someone would run a Qt desktop app.

So I stripped the fallback entirely. The app checks for AES-NI at startup. If it's not there, it shows a clear error and refuses to proceed. One cipher, no migration paths, no cross-device compatibility bugs.

The Codex regression finding actually proved the point: dual-cipher support is a source of bugs, not safety. The simpler design is the more secure one.

## What got fixed

Across three priority tiers (P0/P1/P2), ten issues total:

**The fundamentals (P0):**
- BIP39 recovery phrase validation — the app previously accepted any 12 words that started with a letter. Now it validates against the full 2048-word wordlist with checksum verification.
- Deterministic Argon2 salt — was SHA-256 of the mnemonic, which enables precomputation attacks. Replaced with a random salt stored in the database.
- PIN brute-force protection — no retry limit previously. Now 5 attempts, then exponential lockout with a live countdown timer in the UI.

**Privacy hardening (P1):**
- Note titles were stored as plaintext in SQLite while the body was encrypted. Now both are encrypted. The plaintext column gets cleared on migration.
- Temporary key buffers (PIN-derived keys, intermediate values) were ordinary QByteArray objects that persisted in heap memory. Wrapped them in a SecureBuffer class that calls sodium_memzero on destruction.

**Defense in depth (P2):**
- AES-NI availability check (the fail-fast approach described above)
- SQLite PRAGMA secure_delete, DELETE journal mode, 0600 file permissions
- Nonce length validation before AEAD calls

## The lockout timer, unexpectedly

The PIN lockout was supposed to be a backend-only change. Add a counter, return an error message with the number of seconds. Done.

But the UI showed "Wrong PIN. Locked out for 30 seconds." as static text. The number didn't count down. You had to tap Unlock again to see the updated time. It felt broken even though it was technically correct.

So I added a QML Timer that ticks every second, updating the error text and disabling the button with a countdown. Small thing, but it changed the lockout from feeling like a bug to feeling like a feature.

## What I learned about AI code review

Using two different AI systems as reviewer and implementer surfaces a class of bugs that self-review misses. Claude Code knows what it intended to write. Codex doesn't — it just reads the diff cold and asks whether the code does what the comments say it does.

The cipher persistence bug is a perfect example. The implementation was internally consistent. The tests passed. The code was clean. But the design had a cross-cutting assumption (same cipher on all machines) that wasn't visible in any single function.

I'll keep using this loop. The script is at `scripts/security-review-loop.sh` if you want to try it.

## Current state

25 tests passing. Four rounds of security review (two Claude, two Codex). All findings resolved or documented. The only remaining open item is AEAD additional authenticated data (AAD) for domain separation — low priority, future hardening.

Next up: packaging as an AppImage for standalone distribution, and as an LGX package for installation inside Logos App.

---

*This is part of the [Immutable Notes](https://github.com/xAlisher/logos-notes) build log. Previous post: [Phase 0](2026-03-12-phase-0.md).*
