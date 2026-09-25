# Working on Recursed++

Everything here is for building and testing the mod. A player needs none of it: their download is
one executable, and [README.md](README.md) is written for them.

## Build from source

Requires Visual Studio 2022 C++ x86 tools and a Windows SDK. `build.cmd` locates the installation with
`vswhere`, including Community, Enterprise, and Build Tools installations. Lua 5.2.4 is downloaded from
its official source and checked against a fixed SHA-256.

```powershell
./tools/bootstrap.ps1
./build.cmd
./build/ci_test.exe
```

`build.cmd` links `build/recursed_peek.dll` first, then compiles the resources that carry it, so
`build/Recursed-Plus-Plus.exe` always holds the mod from the same run. The window launcher runs that
embedded copy, never a DLL beside it; to iterate on the mod itself use `build/recursed_peek.exe` or
**Start-Preview.cmd**, which both load the loose `build/recursed_peek.dll`.

To run integration tests against your local game:

```powershell
./tools/backup-saves.ps1
./tools/prepare-runtime.ps1
./build/snapshot_test.exe
./build/render_test.exe
./tools/verify-backup.ps1
```

To create the same downloads as CI:

```powershell
./tools/package.ps1              # both; -Kind player or -Kind developer for one
```

That writes `dist/Recursed-Plus-Plus.exe`, `dist/Recursed-Plus-Plus-developer-windows-x86.zip` and
`dist/SHA256SUMS.txt`. The player step refuses to package a launcher whose embedded mod is not the
one `build.cmd` just produced.

GitHub Actions builds on `windows-2022`, runs the authored fixture tests without game files, and
uploads both downloads for every pushed branch, tag, pull request, and manual dispatch. Game-dependent
tests and native rendering validation must run locally. See the [validation notes](docs/validation.md).

## Preview Lab and the other test levels

The mod's own test rooms replace the game's level menu, so they need a separate copy of the game rather
than your installed one. In PowerShell, from a clone of this repository or the extracted developer
bundle:

```powershell
./tools/backup-saves.ps1
./tools/prepare-runtime.ps1
```

For a non-default installation, pass `-SteamDirectory 'D:\Steam'` to the backup script and
`-GameDirectory 'D:\SteamLibrary\steamapps\common\Recursed'` to the preparation script. Then run
**Start-Preview.cmd** and press Enter twice to open **Preview Lab**.

The test menu includes **Preview Lab** (including a room with both return flames), stock **Chests** and
**Flood**, **State Lab** for wet/global state, **Render Lab** for comparing the native room and its
self-referencing preview, and **Paradox Lab**, where carrying the first room's global chest into the
attic and putting it down turns the attic's flame into a paradox.

## Developer diagnostics

These exist only in a run started with `RECURSED_PEEK_DEV=1`, which **Start-Preview.cmd** and the
console launcher set and the window launcher clears, so a player never reaches them.

| Input | Action |
| --- | --- |
| F7 | Redraw the active room with the original renderer, not a chest destination |
| F8 | Restart the level and replay every recorded tick, comparing each with the recording; the log says whether they all matched and how long it took |

`RECURSED_PEEK_TEST_INPUT=1` buffers short SFML key events, which is what lets an automated test drive
the game with posted key messages; it also turns on the cursor diagnostics in the log. The window
launcher clears this too.

`%LOCALAPPDATA%\Recursed++\peek.log` records what the mod did, and is rewritten on every run. The mod
the window launcher runs is unpacked to `%LOCALAPPDATA%\Recursed++\bin`, under a name taken from its
contents, so a game still holding an older one open never blocks a newer build.

## Save backups

`tools/backup-saves.ps1` copies both your Steam progress and this build's own save folder. Backups live
under `backups/<timestamp>/`, with source paths and SHA-256 values in `manifest.json`. The verification
script checks both backups and original files. No restore script automatically overwrites progress.
Exit the game and Steam before any manual restore. Runtime copies, backups, reverse-engineering dumps,
dependencies, and build output are excluded from version control and from both downloads.

## Conventions

English in repository content, [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)
for commit messages, and a [CHANGELOG.md](CHANGELOG.md) entry with every change a player would notice.
The full set of project rules is in [AGENTS.md](AGENTS.md).
