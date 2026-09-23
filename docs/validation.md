# Validation

Validated on 2026-09-22 using the supported Windows x86 executable. Most interactive testing runs the workspace runtime copy with Steam answered inside the process and progress kept in this build's own save folder. The Steam path is what a player gets by default, so it was also exercised against the installed Steam copy and a real account, with `tools/backup-saves.ps1` run first; those runs are called out where they appear.

## CI tests without game files

`build/ci_test.exe` uses authored fixtures under `tests/fixtures` and checks:

- Lua room extraction, chest destinations, tile names, and separate frame numbers.
- Wet branches and liquid queries.
- Saved global-state replacement, empty-list suppression, and deduplication.
- Invalid live pointers and path traversal rejection.
- Authored old-ABI room stacks: ancestor identity, tile indices, portal variants, and actual positions.
- Resampling after movement, flooding, holding an item, and destruction; prior snapshots remain immutable.
- Missing rooms, unavailable file APIs, instruction limits, and object-count limits.
- Partial room results are discarded after a Lua failure.
- Deterministic fallback particle sampling and bounded particle output.

GitHub Actions compiles the x86 DLL, launcher, diagnostic utility, and all test binaries on Windows, runs these fixture tests, and packages the two allowlisted downloads: the single executable a player takes and the developer bundle. It does not download or redistribute Recursed and cannot validate native rendering without the game.

## Local game-dependent tests

`build/snapshot_test.exe` loads 100/100 stock and additional-content starting rooms. It also checks test-room and stock-room object contents, positions and targets, dry/wet branches, global-object merging, meshes, particle definitions, and native tile names.

`build/render_test.exe` checks fallback animation, 30 Hz cache reuse, unchanged terrain pixels and input snapshots, and that a record, fan, generic and cauldron each reach their own asset and mesh entry instead of the schematic fallback, then exports 48 frames under `build/effects-frames`. The tested frames differed in 8,051 pixels. These are fallback-renderer tests, not native-engine output tests.

Passing script extraction does not imply exact puzzle-state simulation or support for every native entity.

## Native rendering GUI checks

- Active-room replay: F7 invokes the original renderer again into a separate framebuffer. This diagnostic is not a destination preview.
- Preview Lab: the left chest displays keyroom using native stone tiles, rotating key, gold lock, record ring, lighting, background, and chest particles.
- O opens the same path in a separate window at Depth 1; see `native-destination.png`.
- Clicking its chest enters pool at Depth 2. The native background changes with depth, and native water baseline and foam are visible; see `native-water.png`.
- Backspace releases the pool scene and returns to keyroom. O docks the preview. Esc closes it without opening the pause menu.
- The real player and room objects remain in place. Native frames are accepted only after the bounded live-state audit passes.
- Tile definition IDs are resolved by name rather than incorrectly treating atlas frame numbers as native IDs.

The two checked-in native screenshots show the earlier UI, before removal of implementation labels. They are historical rendering evidence, not current UI captures.

## Current increment: interactive regression

Computer Use checked the isolated build on 2026-09-22:

- Both red and green flames in `portals` return from Depth 1 to the actual current room at Depth 0.
- Native spawn placement puts preview chests against the floor. The key rests on its platform and the lock uses its native model.
- While a chest is held, the live room preview omits it. Moving and releasing it updates the same open window at its new position: x=7.60 instead of the initial x=7.00.
- Actual entry into `keyroom` invalidates the old preview. Clicking its real return flame displays the existing parent at Depth -1, retaining that moved chest position.
- Clicking a chest in the parent preview moves inward to Depth 0; Backspace returns to Depth -1.
- O switches presentation, and Esc closes the preview without pausing gameplay.
- The popup's O key initially failed under the Chinese IME. Disabling IME association for that shortcut-only window fixed it; O was retested after restoring Chinese input mode in the main window.
- The game and every preview window were closed at the end; no Recursed process remained.

Native spawn placement was also confirmed from the executable: flag mask 0x21, twenty collision moves of 0.05 tiles. The x86 build, asset-free fixtures, and save verification pass. Earlier local snapshot and fallback-render test results remain applicable.

The current [parent-room capture](outside-preview.png) was saved with the pointer outside the captured window. It shows Depth -1 after real entry, without implementation labels or manual liquid controls.

## Record entity increment

Computer Use checked the isolated build on 2026-09-22 after adding native Record support:

- Stock Chests/basic5 `under`, the room this increment targets, now draws through the original renderer: the record shows its ring model, the return flame is native fire, and the key rotates across frames. Previously the one unsupported record sent the whole scene to the fallback renderer, which drew the record as a schematic dot and the flame as an outlined ellipse.
- Preview Lab keyroom: the record settles onto the same platform as the key instead of hanging in free air.
- The game and its preview windows were closed at the end; no Recursed process remained.

## Remaining entity kinds increment

Computer Use checked the isolated build on 2026-09-22 after adding fan, generic, cauldron, bird, jar and froth:

- The authored `props` room previews natively: the return flame is native fire, the fan turns, the oobleck blob and the cauldron draw their own models, and the run log records `objects=5` with no rejection and no audit failure.
- The bird is constructed and placed, and its position stays empty on screen, which is what `Bird::draw` returning on a null +0x4C predicts.
- Measured against shipped mission scripts, rooms that fall back because of an unsupported object drop from 110 of 354 to 1 of 354. The remainder is `apex` in `dark6.lua`, the only room in the game that declares a crux. Bird rooms now render everything except the bird itself.
- The game and its preview windows were closed at the end; no Recursed process remained.

Not observed on screen: a jar or a froth, because neither has a Lua spawn path and both reach a preview only through a live outer room; and a stock cauldron room, because the cauldron chapter is not on the test menu. The authored cauldron exercises the same constructor and the same +0x4C destination read.

## Earlier state and interaction checks

- State Lab dry and submerged chests target the same vault but select different automatic conditions.
- Global keys and boxes fall after actual entry; self-referencing previews display their current positions.
- Leaving the vault retains saved positions instead of reverting to initial declarations.
- Taking the global key outside removes it from the destination preview.
- Nested automatic wet inference was checked. The old manual dry/wet control has since been removed.
- Separate-window resize and DPI handling, nested selection, back navigation, docking, and closing were checked.
- Original Chests/basic5 destination detection and the earlier layout preview were checked.

## Cursor visibility change

The supported executable explicitly hides its cursor during window initialization at `0x43E121`. The cursor hook overrides that request, since the pointer is what chests are hovered with, and inspection is always on, so there is no longer a state in which the game gets its way. Switching between game and popup was tested interactively. Test-mode logs read `GetCursorInfo` after the visibility change and report `systemVisible=1`, which distinguishes the actual system cursor from the Computer Use pointer marker. Cursor diagnostics are emitted only with `RECURSED_PEEK_TEST_INPUT=1`.

## Progress storage

The game reads and writes its progress through Steam Cloud only, and its own guard skips the write when Steam is missing, so before this increment an isolated run kept nothing. Checked on 2026-09-22 against the runtime copy:

- With `save0`, `save0-dlc`, and `save0-dlc2` present in the save folder, the run log records `Progress loaded: save0 (1098 bytes)` as the game leaves its title screen.
- Leaving the menu and closing the game each record `Progress saved: save0 (1098 bytes)`, and the rewritten file is byte-identical to the Steam original it came from.
- The launcher's **Import from Steam** reported three files copied from the Steam account found in `userdata`, and the progress it replaced was kept in `replaced-20260922-200627`.
- `build/ci_test.exe` calls the same interface entries the game calls, at the same vtable offsets, and covers the write/read round trip, a name that tries to leave the save folder, which Steam Cloud files are worth importing, and the replaced-progress copy.
- The game's other Steam entry points are answered in the process and never reach Steam: the context it is given carries storage alone, and its user, utility, and stats entries stay null, which its own null checks already cover, so no achievement or stat call is made.

Opening the save folder from the launcher uses the shell. On the test machine Explorer refused newly created folders while it was in a stale state, including from a hand-typed path, which is an Explorer condition rather than a launcher one; the launcher reports the path either way.

## Steam, the window, and input

Checked on 2026-09-22 against both the runtime copy and the installed Steam copy:

- Started from the launcher with its default, the run logs `Steam connected; achievements and cloud saves are the game's own`, and the cloud save it read was not rewritten by a session that ended at the title screen.
- Started isolated, the same build logs `Steam not used; progress kept in this build's save folder` and keeps the round trip described above. The launcher passes the choice through the environment; the plugin decides from it and from whether Steam actually answered.
- The window of a modded run is titled Recursed++, which is also how the screenshot in the README was identified.
- Escape with a preview open closes the preview and leaves the game running; the pause menu does not appear. The game reads the key state before the event that announces the press, which is why answering the event alone was not enough and the key state itself is answered while a preview is open.
- Escape with no preview open still opens the game's own pause menu.
- The mouse back button steps from `pool` back to `keyroom`, and the forward button returns to `pool`, over the game and in the separate window.
- Nothing is drawn over the game until something is hovered: the banner and the key reminders are gone, and the preview panel carries the room, the depth and any error only.

## One executable, and what a player does not get

Checked on 2026-09-22:

- `build/Recursed-Plus-Plus.exe` grew from 1,300,992 to 3,466,240 bytes, which is the launcher plus the 1,789,440-byte mod it now carries.
- Copied on its own into an empty folder and started there, it unpacked `%LOCALAPPDATA%\Recursed++\bin\recursed_peek-<hash>.dll` and the modded game came up with Steam connected. Nothing else was in the folder.
- The unpacked name is taken from the contents, so a second launch while an older game still holds its copy open writes a different file instead of failing on a locked one. A copy that is already there is read back and checked against that name before it is reused: the second launch started the game without rewriting the file, and its timestamp did not move.
- The launcher keeps the unpacked copy open for the rest of the run, which keeps another launcher's sweep and an antivirus from taking it between unpacking and loading. The game still loads it: sharing the file for reading is what the loader needs.
- Started with `RECURSED_PEEK_DEV=1` and `RECURSED_PEEK_TEST_INPUT=1` set in the shell, the launcher still reported `Automated test input buffering: off; developer diagnostics: off` in the game it started: a player's run states that environment rather than inheriting it, so F7 cannot be reached from a player download.
- `tools/package.ps1` refuses to publish a launcher whose embedded bytes are not the `build/recursed_peek.dll` of that build, which is the failure a reordered or half-finished build would otherwise ship silently.
- `tools/check-repository.ps1` fails if the resources stop carrying the mod or the notices, if the window launcher starts naming a DLL beside itself, if a developer diagnostic loses its flag, or if README.md starts pointing a player at the developer bundle's files.
- The launcher's Notices page shows THIRD_PARTY_NOTICES.md from inside the executable, which is what carries Lua's licence with the copy now that there is no archive.
- Not exercised: a default run on a machine where Steam is installed but not running, which is the branch that logs `Steam did not answer` and keeps that session in the save folder. The launcher warns before starting it, and the other two branches of the same decision are covered above.
- The executable CI built was downloaded from its own run, put in a folder holding nothing else, and started the modded game with Steam connected. A second launch from the same folder reused the unpacked copy without rewriting it, and the copy an earlier build had left behind was swept.
- A game started from the single executable keeps running after the launcher window is closed, which is what the job the launcher puts it in has to allow once the mod is in: that job exists only to take a suspended game with it if the launcher dies before it can be resumed.
- `tools/check-repository.ps1` was checked against a deliberate break: renaming one of the two resources in src/embedded_plugin.cpp fails it with the name that no longer matches, and the check passes again once reverted.

## Pinned hover depth, right click, and the fallback flash

Computer Use checked the isolated build on 2026-09-23, with Steam not used. The saves were backed up with `tools/backup-saves.ps1` and verified with `tools/verify-backup.ps1` first. For the on-screen run only, a temporary build logged every preview frame drawn without the original renderer, and every change to the depth line. The logging is not part of the change.

- The runtime copy of Preview Lab starts with a UTF-8 byte-order mark. The game loaded it, but every preview of it read "Preview unavailable" with a Lua error at line 1, and `snapshot_test` and `render_test` failed at their first load. The mod now skips the mark the way the game's own loader does, `build/ci_test.exe` covers it, and both game-dependent tests pass against that copy: 100/100 stock starting rooms, and the same 8,051 changed pixels as before.
- Hovering the keyroom chest: the first frame waited and drew nothing, and the next frame drew the native panel. No frame in the whole run was drawn with the stand-in renderer. Five frames held the previous panel while a new room was drawn.
- Pinned keyroom, pointer on its chest: "PINNED DEPTH 1   HOVER DEPTH 2: POOL". On its flame: "HOVER DEPTH 0: START". In pool: "PINNED DEPTH 2   HOVER DEPTH 3: KEYROOM". Clicking went to depth 3. After Backspace, pool's flame read "HOVER DEPTH 1: KEYROOM", and clicking it returned to keyroom at depth 1 with two rooms kept for Forward.
- Right click over the pinned inset closed it. Right click on a pinned chest closed it, and that chest did not reopen as a hover preview until the pointer reached another chest, whose preview then appeared. Right click in the separate window, titled "Recursed++ | Depth 1 | props", closed the window and the preview. Backspace at the root from the separate window closed it and left the chest under the pointer dismissed.
- With the Chinese input method active, O went to the input method and Shift switched its language, so Shift + click did not reach the game. After that switch, O opened the window. This is the input-method case the README already describes.
- Not observed on screen: the mouse forward button, which Computer Use cannot press, a pinned chest carried out of a room, and the pin click over a chest the inset covers. The carried case rests on the executable: leaving through a flame pops the room stack at 0x440250 and never reaches the room builder at 0x440A20, whose only caller is entry at 0x43FCE0. That is why the pin survives, and why steps now store relative depth.
- `tools/check-repository.ps1` passes. The build compiles without warnings at `/W4`.
- The game and its preview window were closed at the end, and no Recursed process remained.

## Undo increment

Checked on 2026-09-23 against the runtime copy, isolated from Steam, in Preview Lab reached from the level map. Undo sat on F9 and F11 for most of these runs and was then moved to Q and W; the replay behind both is the same, and the last runs below used Q and W. Every undo goes through the game's own Restart, which rewrites the save, so the saves were backed up with `tools/backup-saves.ps1` and checked with `tools/verify-backup.ps1` first. The game was driven by key messages posted to its window with `RECURSED_PEEK_TEST_INPUT=1`, and captured with `PrintWindow`, so no pointer was over the game. F8, in a development run, restarts the level, replays every recorded tick and compares each tick's room digest with the one recorded; a mismatch would stop the replay at that tick and say so in the log. None did.

| Case | Ticks replayed | Replay time |
| --- | --- | --- |
| Walk from rest, F9 | 504 | 0.6 ms |
| F8 after entering keyroom and moving there | 2224 | 4.1 ms |
| F8 after picking up a chest and throwing it at a wall | 4266 | 4.9 ms |
| F11, five seconds back | 4938 | 5.7 ms |
| F8 after start, pool, swimming, then keyroom inside pool | 3281 | 5.3 ms |
| F8 after the oobleck in props set into a solid object, fan, cauldron, bird and crux present | 5472 | 11.0 ms |
| F8 after two minutes of seeded random walking, jumping, grabbing and throwing in props | 11452 | 32.0 ms |

- F9 after walking put the player back on the starting spot. Three F9 presses after entering keyroom went back through the jump inside it, the walk inside it, and the jump into the chest, landing in the start room where that jump began.
- After each F8 the room was the same on screen as before it: the player, the thrown chest against the wall, the set oobleck and the bird in the same places. Only animation phases differed, and the bird's subtitle was gone, because the replay is silent.
- The pause menu's own Restart started a new recording, without a replay. F9 with nothing done yet showed "NOTHING TO UNDO" and did not restart.
- Two F9 presses that arrived in one batch of events went back two actions.
- Every undo logged `Progress saved: save0 (1098 bytes)`, the game's own Restart rewriting the unchanged save.
- Q after a jump and a walk went back before the walk, and Q again before the jump; W with under five seconds played went back to the start of the level.
- With Right still held through an undo, the next Q went back past the walk that resumed, rather than returning to the same moment. Two Q presses made inside the pause menu did nothing when play resumed.
- With the profile's `controls` setting binding Q to Jump, in the game's own comma-separated format, Q jumped and did not undo, and the log said why; W still went back. The profile's `recursed.ini` was copied aside first and restored afterwards, hash unchanged.
- Not exercised: RB and RT, since no gamepad was attached; Q and W forwarded from the separate preview window, which needs a pointer over a chest to open it; and the crux hum after an undo in its own room, since the captures have no sound.

The digests cover positions, velocities, contact bits, the player's move state, the held item and a few kind-specific fields, never a heap pointer. They are not a proof that every field matches, only that nothing they cover ever differed.

## Save isolation

The local backup verifier confirmed eleven backed-up files and unchanged original hashes, covering both the Steam progress and this build's own save folder. The verifier had been reading a multi-entry manifest as one entry, which made it fail on any backup of more than one file; it now walks the entries. Backups, manifests containing private paths, and runtime files are not distributed. Users should run `tools/backup-saves.ps1` and `tools/verify-backup.ps1` on their own installation.

## Not covered

Full ongoing gravity/buoyancy simulation, all global restoration collision cases, carried-item branches, state changes across successive hypothetical entries, preserved jar instances, cauldron transition rules, every native entity destructor, and other executable versions remain outside the verified scope. Jars and cauldrons now render natively; only their state semantics are unverified. The gameplay-field audit does not cover every engine field. Unsupported native scenes use resource-based rendering; normal UI omits implementation labels.
