param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'dist' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$stage = Join-Path $OutputDirectory ('stage-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
# Explicit allowlist: never include runtime, saves, logs, or proprietary assets.
$files = @(
    'build/Recursed-Plus-Plus.exe', 'build/recursed_peek.exe', 'build/recursed_peek.dll',
    'Start-Preview.cmd', 'README.md', 'CHANGELOG.md', 'THIRD_PARTY_NOTICES.md', 'DESIGN.md', 'FEASIBILITY.md',
    'docs/validation.md', 'docs/chest-preview.png', 'docs/outside-preview.png',
    'tools/prepare-runtime.ps1', 'tools/backup-saves.ps1', 'tools/verify-backup.ps1',
    'tests/peek-lab.lua', 'tests/state-lab.lua', 'tests/render-lab.lua', 'tests/nexus.lua'
)
foreach ($relative in $files) {
    $target = Join-Path $stage $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot $relative) -Destination $target
}
$checksums = foreach ($relative in $files) {
    '{0}  {1}' -f (Get-FileHash -LiteralPath (Join-Path $stage $relative) -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'), $checksums)
$archive = Join-Path $OutputDirectory 'Recursed-Plus-Plus-windows-x86.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
Write-Output $archive
