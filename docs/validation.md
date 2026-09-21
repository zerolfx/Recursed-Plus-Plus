# Validation

Validated on 2026-09-22 using the supported Windows x86 executable. Interactive testing runs only the workspace runtime copy with Steam initialization disabled and configuration isolated.

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

`build/snapshot_test.exe` loads 100/100 stock and additional-content starting rooms. It also checks test-room positions and targets, dry/wet branches, global-object merging, meshes, particle definitions, and native tile names.

`build/render_test.exe` checks fallback animation, 30 Hz cache reuse, unchanged terrain pixels and input snapshots, then exports 48 frames under `build/effects-frames`. The tested frames differed in 8,051 pixels. These are fallback-renderer tests, not native-engine output tests.

Passing script extraction does not imply exact puzzle-state simulation or support for every native entity.

## Native rendering GUI checks

- Active-room replay: F7 invokes the original renderer again into a separate framebuffer. This diagnostic is not a destination preview.
- Preview Lab: the left chest displays keyroom using native stone tiles, rotating key, gold lock, lighting, background, and chest particles.
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

## Earlier state and interaction checks

- State Lab dry and submerged chests target the same vault but select different automatic conditions.
- Global keys and boxes fall after actual entry; self-referencing previews display their current positions.
- Leaving the vault retains saved positions instead of reverting to initial declarations.
- Taking the global key outside removes it from the destination preview.
- Nested automatic wet inference was checked. The old manual dry/wet control has since been removed.
- Separate-window resize and DPI handling, nested selection, back navigation, docking, and closing were checked.
- Original Chests/basic5 destination detection and the earlier layout preview were checked.

## Cursor visibility change

The supported executable explicitly hides its cursor during window initialization at `0x43E121`. The cursor hook now overrides that request while inspection is enabled and restores it when F8 disables inspection. F8 off/on and switching between game and popup were tested interactively. Test-mode logs read `GetCursorInfo` after the visibility change: `systemVisible=0` when inspection is disabled and `systemVisible=1` when enabled. This distinguishes the actual system cursor from the Computer Use pointer marker. Cursor diagnostics are emitted only with `RECURSED_PEEK_TEST_INPUT=1`.

## Save isolation

The local backup verifier confirmed four backed-up files and unchanged original hashes after native preview testing. Backups, manifests containing private paths, and runtime files are not distributed. Users should run `tools/backup-saves.ps1` and `tools/verify-backup.ps1` on their own installation.

## Not covered

Full ongoing gravity/buoyancy simulation, all global restoration collision cases, carried-item branches, state changes across successive hypothetical entries, jars, cauldrons, every native entity destructor, and other executable versions remain outside the verified scope. The gameplay-field audit does not cover every engine field. Unsupported native scenes use resource-based rendering; normal UI omits implementation labels.
