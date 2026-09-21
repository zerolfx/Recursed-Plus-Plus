# Recursed++

A Windows x86 mod that lets you inspect rooms inside chests, inspired by the visible nesting in Patrick's Parabox. Hover to preview, click to pin, or open a separate window and explore up to eight levels deep.

![Native rendering of a nested room](docs/native-water.png)

The preview now uses Recursed's original renderer for terrain, depth-dependent backgrounds, lighting, models, chest particles, key rotation, and water effects. Room state remains approximate; rendering fidelity and entry-state simulation are separate concerns.

## Download and run

1. Open this repository's **Actions** tab and select a successful **Build Windows patch** run.
2. Download the `Recursed-Plus-Plus-windows-x86` artifact, then extract the patch archive inside it to a writable directory.
3. Install Recursed through Steam. Only the executable fingerprint below is supported.
4. In PowerShell, from the extracted directory, prepare your local copy:

```powershell
./tools/backup-saves.ps1
./tools/prepare-runtime.ps1
```

For a non-default installation, pass `-SteamDirectory 'D:\Steam'` to the backup script and `-GameDirectory 'D:\SteamLibrary\steamapps\common\Recursed'` to the preparation script.

5. Run **Start-Preview.cmd**. Press Enter twice to open **Preview Lab**.

The archive contains the mod, launcher, scripts, and authored test levels. It contains no game executable, game assets, or save files. The game must be supplied from your own installation.

**This is an experimental, isolated preview build, not a normal progression-saving setup.** The launcher starts only its own `runtime/Recursed.exe` copy, disables Steam initialization in that process, and redirects configuration to `build/test-profile/`. It never attaches to an existing game process. Launching the original game through Steam does not load the mod.

## Controls

| Input | Action |
| --- | --- |
| Hover over a chest | Show its destination layout |
| Left click | Pin the preview in the game window |
| Shift + left click | Open a separate preview window |
| O | Move the preview between the game and a separate window |
| Click a chest inside the pinned preview | Explore another level, up to depth 8 |
| Backspace | Return one level; close at the root |
| W | Cycle automatic, forced dry, and forced wet conditions |
| Esc | Close the preview without pausing the game |
| F8 | Toggle inspection |
| F7 | Developer diagnostic: redraw the active room, not a chest destination |

The separate window supports resizing and maximization, preserves a 4:3 image, and displays depth, room name, and wet/dry conditions in its title. If an input method intercepts letter keys, switch to an English layout or use Ctrl + O. Chests have a small hover underline instead of persistent bounding boxes.

The test menu includes **Preview Lab**, stock **Chests** and **Flood**, **State Lab** for wet/global state, and **Render Lab** for comparing the native room and its self-referencing preview.

## What is rendered

Supported scenes create a private native Room, entities, and Renderer. The original rendering pipeline draws to a separate framebuffer at up to 30 Hz. Supported objects are chests, keys, locks, boxes, and crystal/diamond/ruby collectibles. Terrain and water surfaces use native tile definitions, not guessed sprite-frame indices.

Physics does not advance. Objects remain at snapshot positions while visual effects animate. No player or return portal is created at the entrance. Rooms containing unsupported objects, such as jars, cauldrons, or birds, use the resource-based fallback renderer and display `FALLBACK`; the separate window reports the reason.

![Native keyroom preview](docs/native-destination.png)

The first level reads the actual chest's wet flag. Deeper levels infer wetness from snapshot tiles. Saved global objects replace initial declarations, and self-referencing previews read the current room's global objects. Held or destroyed objects are excluded where identified. Unvisited rooms retain their initial declarations.

The preview is **not a live simulation of an existing destination instance**. Ordinary chest entry constructs a fresh room in the original game, so the initial room script remains relevant even with native rendering. Collision resolution during global restoration, carried-item script branches, consecutive hypothetical entries, preserved jar instances, and cauldron rules are not fully modeled. The UI therefore retains `APPROXIMATE` state labels. The main game continues running while you inspect.

## Build from source

Requires Visual Studio 2022 C++ x86 tools and a Windows SDK. `build.cmd` locates the installation with `vswhere`, including Community, Enterprise, and Build Tools installations. Lua 5.2.4 is downloaded from its official source and checked against a fixed SHA-256.

```powershell
./tools/bootstrap.ps1
./build.cmd
./build/ci_test.exe
```

To run integration tests against your local game:

```powershell
./tools/backup-saves.ps1
./tools/prepare-runtime.ps1
./build/snapshot_test.exe
./build/render_test.exe
./tools/verify-backup.ps1
```

To create the same patch archive as CI:

```powershell
./tools/package.ps1
```

GitHub Actions builds on `windows-2022`, runs authored fixture tests without game files, and uploads the patch ZIP on pushes to `main`, version tags, pull requests, and manual dispatches. Game-dependent tests and native rendering validation must run locally. See [validation notes](docs/validation.md).

Use English in repository content and [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) for commit messages, such as `feat(preview): render isolated rooms with the native engine`.

## Compatibility and save backups

Supported `Recursed.exe` SHA-256:

```text
0E47D5DF0F45152978777E5F8EC7F2435BAB79CD84D630CFAAD5106B90D74251
```

The launcher validates the executable, and the DLL checks instruction signatures and vtables. Other builds are rejected. The original installation is not modified.

Backups live under `backups/<timestamp>/`, with source paths and SHA-256 values in `manifest.json`. The verification script checks both backups and original files. No restore script automatically overwrites progress. Exit the game and Steam before any manual restore. Runtime copies, backups, reverse-engineering dumps, dependencies, and build output are excluded from version control and patch packaging.

## Implementation

- `src/plugin.cpp`: SFML display/input hooks, chest detection, preview navigation, and the in-game overlay.
- `src/snapshot.cpp`: bounded Lua VM capturing room declarations and wet branches.
- `src/runtime_state.cpp`: read-only room-stack and global-object extraction for the supported build.
- `src/native_scene.cpp`: isolated native scene ownership, tile mapping, and original entity construction.
- `src/native_render.cpp`: original renderer hook, private framebuffer, animation clock, and gameplay-field audit.
- `src/room_art.cpp`, `src/asset_mesh.cpp`, `src/particle_sim.cpp`: resource-based fallback.
- `src/preview_window.cpp`: the separate Win32 preview window.

The native path borrows read-only level metadata while owning its room stack, scene entities, and rendering resources. It isolates random-number use and room numbering. The normal game draw runs last. A failed gameplay-field audit disables native previews for that process; this bounded audit is not a proof that every engine field is unchanged.

Logs are written to `build/peek.log`. Automated local input tests may set `RECURSED_PEEK_TEST_INPUT=1` to buffer short SFML key events; the normal launcher script clears it.

See [design](DESIGN.md), [reverse-engineering notes](FEASIBILITY.md), and [third-party notices](THIRD_PARTY_NOTICES.md). This is an unofficial mod and is not affiliated with Recursed's developers.
