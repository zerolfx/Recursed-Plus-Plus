# Validation

Validated on 2026-09-22 using the supported Windows x86 executable. Interactive testing runs only the workspace runtime copy, with Steam unreachable, progress kept in this build's own save folder, and configuration isolated.

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

GitHub Actions compiles the x86 DLL, launcher, diagnostic utility, and all test binaries on Windows, runs these fixture tests, and packages an allowlisted patch ZIP. It does not download or redistribute Recursed and cannot validate native rendering without the game.

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

The supported executable explicitly hides its cursor during window initialization at `0x43E121`. The cursor hook overrides that request, since the pointer is what chests are hovered with. Switching between game and popup was tested interactively. Test-mode logs read `GetCursorInfo` after the visibility change: `systemVisible=0` when inspection is disabled and `systemVisible=1` when enabled. This distinguishes the actual system cursor from the Computer Use pointer marker. Cursor diagnostics are emitted only with `RECURSED_PEEK_TEST_INPUT=1`.

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

## Save isolation

The local backup verifier confirmed eleven backed-up files and unchanged original hashes, covering both the Steam progress and this build's own save folder. The verifier had been reading a multi-entry manifest as one entry, which made it fail on any backup of more than one file; it now walks the entries. Backups, manifests containing private paths, and runtime files are not distributed. Users should run `tools/backup-saves.ps1` and `tools/verify-backup.ps1` on their own installation.

## Not covered

Full ongoing gravity/buoyancy simulation, all global restoration collision cases, carried-item branches, state changes across successive hypothetical entries, preserved jar instances, cauldron transition rules, every native entity destructor, and other executable versions remain outside the verified scope. Jars and cauldrons now render natively; only their state semantics are unverified. The gameplay-field audit does not cover every engine field. Unsupported native scenes use resource-based rendering; normal UI omits implementation labels.
