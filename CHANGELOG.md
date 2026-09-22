# Changelog

What changed for someone playing with this mod, newest first. Dates are the day the change
landed. Entries describe the behaviour, not the patch.

## Unreleased

### Fixed

- Progress is no longer lost between runs. Recursed writes its progress through Steam Cloud and
  nowhere else, and skips the write when Steam did not answer, so a run with Steam switched off
  kept nothing at all. The game is now given storage of its own, backed by files under
  `%LOCALAPPDATA%\Recursed++\saves` in the same format Steam Cloud uses.
- Escape closes an open preview without also opening the game's pause menu. The game reads the
  key state ahead of the event that announced the press, so the press has to be answered before
  that event arrives.

### Added

- The launcher plays through Steam by default: achievements, statistics and cloud saves are the
  ones you already have. When Steam is not running, that session keeps its progress in the mod's
  own save folder instead.
- An Advanced page in the launcher, for the choices that change where progress goes: **Play
  isolated**, which keeps the game away from Steam entirely, **Import from Steam**, which copies
  your Steam progress into the mod's save folder and keeps whatever it replaces in a dated folder
  beside it, and **Save folder**, which opens that folder.
- The mouse's back and forward buttons walk the preview history in either direction, over the
  game and in the separate preview window.

### Changed

- The download is one file. `Recursed-Plus-Plus.exe` carries the mod inside itself and unpacks it
  under `%LOCALAPPDATA%\Recursed++\bin` when you press Play, so there is nothing to extract and
  nothing to keep together.
- The authored test levels, the scripts that prepare a runtime copy, the console launcher and the
  F7 diagnostic are no longer part of what a player downloads. They are in a separate developer
  bundle, and the diagnostic only exists in a run started for development.
- The launcher carries the third-party notices with it, on a Notices page, since the executable is
  now the copy the licence travels with.
- Starting a second game while one is already running asks first, because two of them write their
  whole progress back independently and the one that closes last decides what happened.
- The modded game's window is titled Recursed++, so it is not mistaken for an ordinary run.
- Nothing is drawn over the game any more except a preview you asked for: the banner and the
  key reminders are gone, and the controls live in the launcher window.
- Inspection is always on. F8 no longer toggles it, and the pointer stays visible because it is
  what chests are hovered with.
- `tools/backup-saves.ps1` also backs up the mod's own save folder, and `tools/verify-backup.ps1`
  no longer fails on a backup of more than one file.
