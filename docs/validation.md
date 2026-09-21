# Validation

Validated on 2026-09-22 using the supported Windows x86 executable. Interactive testing runs only the workspace runtime copy with Steam initialization disabled and configuration isolated.

## CI tests without game files

`build/ci_test.exe` uses authored fixtures under `tests/fixtures` and checks:

- Lua room extraction, chest destinations, tile names, and separate frame numbers.
- Wet branches and liquid queries.
- Saved global-state replacement, empty-list suppression, and deduplication.
- Invalid live pointers and path traversal rejection.
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

## Earlier state and interaction checks

- State Lab dry and submerged chests target the same vault but select different automatic conditions.
- Global keys and boxes fall after actual entry; self-referencing previews display their current positions.
- Leaving the vault retains saved positions instead of reverting to initial declarations.
- Taking the global key outside removes it from the destination preview.
- Manual dry/wet cycling and nested automatic wet inference were checked.
- Separate-window resize and DPI handling, nested selection, back navigation, docking, and closing were checked.
- Original Chests/basic5 destination detection and the earlier layout preview were checked.

## Save isolation

The local backup verifier confirmed four backed-up files and unchanged original hashes after native preview testing. Backups, manifests containing private paths, and runtime files are not distributed. Users should run `tools/backup-saves.ps1` and `tools/verify-backup.ps1` on their own installation.

## Not covered

Global restoration collision rules, carried-item branches, state changes across successive hypothetical entries, jars, cauldrons, native return portals, every native entity destructor, and other executable versions remain outside the verified scope. The gameplay-field audit does not cover every engine field. State previews retain `APPROXIMATE` labels; unsupported native scenes use explicit fallback rendering.
