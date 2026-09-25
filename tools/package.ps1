param(
    [string]$OutputDirectory,
    # What to produce. A player downloads one executable; the developer bundle carries the console
    # launcher, the authored test levels and the scripts that prepare a runtime copy.
    [ValidateSet('both', 'player', 'developer')][string]$Kind = 'both'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'dist' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$playerExe = Join-Path $projectRoot 'build/Recursed-Plus-Plus.exe'
$pluginDll = Join-Path $projectRoot 'build/recursed_peek.dll'
$outputs = @()

if ($Kind -ne 'developer') {
    # The launcher carries the mod as a resource, and the resource is only as fresh as the build
    # that produced it: rc reads the DLL from disk, so a reordered or half-finished build can leave
    # a launcher that runs yesterday's mod and says nothing. Compare the bytes, not the presence.
    $latin1 = [Text.Encoding]::GetEncoding(28591)
    $haystack = $latin1.GetString([IO.File]::ReadAllBytes($playerExe))
    $needle = $latin1.GetString([IO.File]::ReadAllBytes($pluginDll))
    if ($haystack.IndexOf($needle, [StringComparison]::Ordinal) -lt 0) {
        throw 'Recursed-Plus-Plus.exe does not carry this build of recursed_peek.dll. Run build.cmd again.'
    }
    Copy-Item -LiteralPath $playerExe -Destination (Join-Path $OutputDirectory 'Recursed-Plus-Plus.exe') -Force
    $outputs += 'Recursed-Plus-Plus.exe'
}

if ($Kind -ne 'player') {
    $stage = Join-Path $OutputDirectory ('stage-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    # Explicit allowlist: never include runtime, saves, logs, or proprietary assets. None of this
    # reaches a player; it is what someone building or testing the mod needs.
    $files = @(
        'build/Recursed-Plus-Plus.exe', 'build/recursed_peek.exe', 'build/recursed_peek.dll',
        'Start-Preview.cmd', 'README.md', 'CONTRIBUTING.md', 'CHANGELOG.md', 'THIRD_PARTY_NOTICES.md',
        'DESIGN.md', 'FEASIBILITY.md',
        'docs/validation.md', 'docs/chest-preview.png', 'docs/outside-preview.png',
        'docs/native-destination.png', 'docs/native-water.png',
        'tools/prepare-runtime.ps1', 'tools/backup-saves.ps1', 'tools/verify-backup.ps1',
        'tests/peek-lab.lua', 'tests/state-lab.lua', 'tests/render-lab.lua', 'tests/paradox-lab.lua', 'tests/nexus.lua',
        'AGENTS.md'
    )
    # A file added under tools/, tests/ or docs/ is developer material by default, and silently
    # leaving it out of the bundle is how a test level or a script goes missing for the next
    # person. Every one of them is either shipped or listed here as deliberately not shipped.
    $notShipped = @(
        'tools/bootstrap.ps1', 'tools/check-repository.ps1', 'tools/package.ps1',
        'tools/disasm.cjs', 'tools/inspect.cjs', 'tools/probe.cpp',
        'tests/ci_test.cpp', 'tests/snapshot_test.cpp', 'tests/render_test.cpp',
        'tests/live_state_fixture.h', 'tests/exit_fixture.h', 'tests/fixtures/custom/missions/test.lua',
        'tests/fixtures/custom/missions/timelines.lua',
        'tests/fixtures/data/tiles/test.lua'
    )
    $tracked = @(git -C $projectRoot ls-files 'tools/*' 'tests/*' 'docs/*')
    if ($LASTEXITCODE -ne 0) { throw 'Could not list tracked files.' }
    foreach ($relative in $tracked) {
        if ($files -notcontains $relative -and $notShipped -notcontains $relative) {
            throw "$relative is neither in the developer bundle nor listed as deliberately left out of it."
        }
    }
    foreach ($relative in $files) {
        $target = Join-Path $stage $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $projectRoot $relative) -Destination $target
    }
    $archive = Join-Path $OutputDirectory 'Recursed-Plus-Plus-developer-windows-x86.zip'
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
    Remove-Item -LiteralPath $stage -Recurse -Force
    $outputs += 'Recursed-Plus-Plus-developer-windows-x86.zip'
}

# One checksum file for whatever this run produced, so a download can be checked against it.
$checksums = foreach ($name in $outputs) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath (Join-Path $OutputDirectory $name) -Algorithm SHA256).Hash.ToLowerInvariant(), $name
}
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'), $checksums)
foreach ($name in $outputs) { Write-Output (Join-Path $OutputDirectory $name) }
