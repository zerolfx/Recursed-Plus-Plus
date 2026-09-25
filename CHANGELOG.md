# Changelog

What changed for someone playing with this mod, newest first. Dates are the day the change
landed. Entries describe the behaviour, not the patch.

## Unreleased

### Added

- A return flame that leads into a paradox now previews the paradox room. When the chest you
  came in through is no longer in the room outside, for instance because you carried it into the
  room it leads to and put it down, walking out takes you out of every room you were in and
  starts you afresh in the level's paradox room. Hovering the flame used to show the room
  outside anyway; it now shows the paradox room, marked Paradox, in its own colours and with the
  objects it keeps from earlier visits. Picking the chest up again, or putting it back, turns the
  preview back to the room outside, and a flame clicked inside a preview is followed the same way.
- Rooms inside a paradox room count their depth from it, as Paradox depth 1 and deeper, since
  the rooms you came through are left behind.

### Fixed

- A return flame's preview of the room outside now includes the global objects that room gets
  back when you walk out, such as a global chest you went in through. The game puts them aside
  while you are inside, and the preview showed the room without them.
- Levels that colour their rooms separately, such as the ones with a paradox room, now get those
  colours wherever the game's renderer cannot draw a preview and the stand-in drawing is used;
  it drew their background black.

## 1.2.0 - 2026-09-23

### Added

- Undo. Q, or RB on a gamepad, takes back your last action, whether a jump, picking something
  up or throwing it, or setting off from standing still, and pressing it again goes back one
  more. W, or RT, goes back five seconds of game time. Undo reaches back to the start of the
  level or to your last Restart, across any rooms you went in or out of on the way. Going back
  happens at once and silently: every sound still playing stops, and nothing already counted in
  Steam is counted again. Like any room change, it closes an open preview. With nothing to go
  back to, the game says so. A key or button you have made one of the game's own controls stays
  that control and does not undo.

### Fixed

- A gamepad connected after the game has started now works. The game only ever looked for
  controllers once, when it started, so one plugged in or switched on later did nothing until
  the game was restarted; now it is picked up as soon as it connects, with the same controls.

## 1.1.0 - 2026-09-23

### Fixed

- A new preview no longer flashes a rough stand-in for a moment before the room appears, the one
  that drew a return flame as a plain oval. It now appears once the game has drawn it, and a
  preview that is already up keeps its picture until the next room is ready. The stand-in is
  only used for a room the game's own renderer cannot draw.
- The mouse's forward button no longer jumps into a room from a path you had already left.
  Clicking a return flame back to the previous room now counts as going back, so forward returns
  to where you were, and opening a new room or pinning another chest starts a fresh history.
- A pinned preview of a chest you carry out of the room keeps the right depths. Walking out does
  not rebuild the room, so the preview stays pinned, and the rooms opened inside it used to read
  one level too deep.
- The click that pins a preview no longer also opens whatever the new preview shows under the
  pointer, and the chests a pinned preview covers can no longer be hovered or Shift-clicked
  through it. Both could happen wherever the preview lay over a chest in the room.
- A quick second click in the separate preview window no longer opens a chest from the room it
  was just leaving.
- Rooms from a mission file saved with a byte-order mark, which Notepad adds, can be previewed.
  The game always read such a file, but every preview of it said "Preview unavailable".

### Added

- Hovering a chest or return flame inside a preview pinned in the game window now shows the
  depth and room a click there would open, beside the pinned depth. Before, the depth line only
  ever described the pinned room, so it looked as if it had stopped updating.
- Right click closes a pinned preview, over the game and in the separate preview window. Closing
  a preview no longer opens another one straight away for a chest that happened to be under the
  pointer; that chest waits until the pointer moves to a different one.

## 1.0.0 - 2026-09-22

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
- Starting without Steam asks first as well: the game cannot reach the progress you normally play,
  so that session would start from the mod's own save folder and stay there.
- Closing the launcher while it is still starting the game no longer leaves a game behind that has
  no window and never exits: the window stays open until the game is running on its own.
- An import now leaves the save folder holding that Steam account's progress and nothing else, says
  when only part of it could be copied, and no longer reports "1 save files".
- The modded game's window is titled Recursed++, so it is not mistaken for an ordinary run.
- Nothing is drawn over the game any more except a preview you asked for: the banner and the
  key reminders are gone, and the controls live in the launcher window.
- Inspection is always on. F8 no longer toggles it, and the pointer stays visible because it is
  what chests are hovered with.
- `tools/backup-saves.ps1` also backs up the mod's own save folder, and `tools/verify-backup.ps1`
  no longer fails on a backup of more than one file.
