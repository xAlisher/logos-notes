# Hotfixes and the Sandbox You Don't See

Shipping v0.5.0 felt good. Settings screen, encrypted backups, deterministic identity — all working in standalone. Then I opened it inside Logos App and everything broke differently.

This post is about the bugs that only exist inside a host application's sandbox, and why testing in one environment doesn't protect you in another.

## The sandbox that eats your features

Logos App loads third-party modules as QML plugins in a sandboxed environment. Your QML file runs, but with restrictions you don't discover until runtime. No error messages. No warnings. Just silence.

The first thing that broke was the file picker. `FileDialog` — a standard Qt component — simply doesn't work inside the sandbox. Click the button, nothing happens. The standalone app opens a native file dialog. The plugin version does nothing.

The storage module in Logos App solves this by being a compiled C++ plugin that can call `QFileDialog` natively. Our module is pure QML. No native dialogs for us.

The fix was to stop pretending we could pick files. Instead, we write backups to a well-known directory (`~/.local/share/logos-notes/backups/`) with a predictable filename. Export writes there. Import lists what's there and lets you pick from a list. No file picker, no sandbox problem.

## The checkbox that wasn't a checkbox

Qt's `CheckBox` component renders as a white square in the Logos App sandbox. No styling, no theme integration, just a bright white rectangle on a dark background. Technically functional. Visually broken.

Replaced it with a custom checkbox — a `Rectangle` with a border that fills orange when checked, with a "✓" text overlay. Twelve lines of QML that look right everywhere.

## State that outlives the screen

Three bugs that all had the same root cause: QML state persisting across screen visits.

The confirmation checkbox in the Danger Zone stayed checked after you navigated away and came back. The export path from your last backup stayed visible on the next Settings visit. The mnemonic field kept your recovery phrase visible after Remove Account brought you back to the Import screen.

All fixed with `onVisibleChanged` handlers that reset state when the screen becomes visible. Simple pattern, easy to forget.

## Masking what matters

The recovery phrase field was a plain `TextEdit` showing twelve words in full. Anyone looking over your shoulder could read your entire key. `TextEdit` doesn't support `echoMode` like `TextField` does, so the standard password-masking approach doesn't work.

The solution: an overlay. The `TextEdit` is transparent when not focused. On top of it sits a `Text` element that shows "••• 12 words entered •••" when you click away. When you click back into the field to type, the overlay hides and the real text appears.

You can see what you're typing. You can't see what you typed after you move on. Good enough for a recovery phrase that gets entered once.

## The QML linter saves hours

The most frustrating bug was the plugin not loading at all inside Logos App. Click on it in the sidebar — nothing. No error. No feedback. Just nothing.

Turns out a dangling `Rectangle {` brace from a code merge left the QML syntactically invalid. The Logos App QML loader silently fails and enters an infinite retry loop. You'd never know from the UI.

`qmllint` catches this instantly. One command, clear output:

```
Main.qml:952:1: Expected token `}'
```

This is now in the pre-install routine. Every change to the plugin QML gets linted before `cmake --install`.

## Testing in two worlds

The lesson from this round: standalone testing covers maybe 60% of bugs. The other 40% only appear inside the host application. Different QML engine configuration, different available components, different file system access, different process lifecycle.

The development routine now has separate checklists for standalone and Logos App, and the Logos App test must happen before merge. Not after. Not "we'll check it later." Before the commit.

## What shipped

v0.5.1 is a hotfix release with nine bug fixes, all discovered during the first real Logos App testing session:

- File I/O moved entirely to C++ plugin
- Custom checkbox, mnemonic masking, state resets
- Backup export to well-known directory with timestamp filenames
- Backup list and cycling for import
- QML syntax validation in the build routine

Twenty-six tests passing. Two environments tested. Zero silent failures remaining.

---

*This is part of the [Immutable Notes](https://github.com/xAlisher/logos-notes) build log. Previous: [Settings, Backup, and the Identity Question](2026-03-14-settings-backup-identity.md).*
