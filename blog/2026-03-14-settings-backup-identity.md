# Settings, Backup, and the Identity Question

This started as a Settings screen and turned into something bigger. The feature request was simple: show account info, let users remove their account properly instead of a dev-mode Reset button. But once you have a Settings screen, you start asking what belongs there. And once you ask that, you end up thinking about identity and portability.

## The Settings screen

The immediate need was straightforward. The Reset button was sitting in the sidebar next to Lock — one click and your entire account is gone. No confirmation, no warning. Fine for development, not fine for real users.

So I built a Settings screen with a Danger Zone. Red border, checkbox that says "I understand all notes will be permanently deleted," button that only activates after you check it. Standard destructive action pattern.

But Settings needs something to show beyond just the delete button. What identifies this account?

## The fingerprint problem

First attempt: SHA-256 of the master key, first 8 bytes as hex. Quick identifier. But the master key is derived from mnemonic + random salt, and the salt changes every time you import. Same recovery phrase, two different devices, two different fingerprints. That's not an identity — it's just a session label.

Second attempt: Ed25519 public key derived from the master key. Same problem — the key depends on the salt.

Third attempt, the right one: derive the public key from the mnemonic itself, with no salt involvement. `SHA-256(normalized_mnemonic)` as the seed for `crypto_sign_seed_keypair()`. Now the same twelve words always produce the same public key, on any device, with any PIN, after any number of re-imports.

This is the identity. Your recovery phrase *is* you, and this public key is the stable proof of that.

## Normalization almost broke everything

Codex caught this one. The BIP39 validation code normalizes the mnemonic (NFKD, whitespace, lowercase) before checking it against the wordlist. But the key derivation and fingerprint code were using the raw string from the text field. Extra space between words? Different key. Capitalized first letter? Different key. Same twelve words, unrecoverable data.

The fix was a shared `normalizeMnemonic()` function called before every crypto operation. Simple, but the kind of bug that would have shown up months later when someone typed their phrase slightly differently on a new device and couldn't restore their backup.

## Encrypted backup

With a stable identity, backup became the obvious next feature. The flow:

1. Settings → Export Backup → saves a `.imnotes` file
2. The file contains all your notes encrypted with your master key, plus the KDF salt
3. On a new device, enter your mnemonic → select the backup file → Import & Restore
4. The app uses the salt from the backup to re-derive your master key and decrypt

The PIN can be different. The salt will be different on the new device. But the same mnemonic + the backup's salt = the same master key = your notes are back.

The filename is `PUBLICKEY_DATE.imnotes` — first 16 hex chars of your public key plus the date. When you select a backup file on the import screen, it parses the public key from the filename and shows it below the title. You can verify you're restoring the right account before entering your phrase.

## The Loader lesson

The first implementation of backup restore didn't work. You'd select a file, import your mnemonic, and land on an empty note screen. The notes weren't there.

The bug: Qt's `Loader` component destroys the previous screen when it switches to a new one. The import screen had the mnemonic text and the backup path in QML properties. By the time the restore code ran (triggered by a screen change signal), the import screen was already destroyed and all its properties were gone.

The fix was to pass the backup path as a parameter to `importMnemonic()` on the C++ side, so the restore happens inside the method call — before the screen switch, before the Loader destroys anything. Data that needs to survive a screen transition must go through the backend, not QML state.

## Restore rollback

Codex also caught that a failed restore left the user stuck. The account was created and marked as initialized before the restore attempt. If the restore failed (wrong mnemonic, corrupted file), the user was dropped into an empty note screen with no way back. Next launch they'd see the unlock screen for an account with no notes.

The fix: `setInitialized()` is now called after a successful restore, not before. If restore fails, the account is rolled back — database wiped, user returned to the import screen with a clear error message. They can try again.

## What's next

The public key opens up future possibilities. It's a stable identifier that could be used for node-to-node backup pinning, shared notebooks, or contact discovery — all without ever revealing the mnemonic. For now it's just a hex string in the Settings screen, but the cryptographic foundation is there.

Next priorities are the AppImage packaging (currently blocked on a Qt QML AOT compilation issue) and the LGX package for installation inside Logos App.

---

*This is part of the [Immutable Notes](https://github.com/xAlisher/logos-notes) build log. Previous: [Security Hardening](2026-03-14-security-hardening.md).*
