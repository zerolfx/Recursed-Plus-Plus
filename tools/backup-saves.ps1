param([string]$SteamDirectory = 'C:\Program Files (x86)\Steam')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$backupRoot = Join-Path $projectRoot ('backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null
$sources = @()
$userData = Join-Path $SteamDirectory 'userdata'
if (Test-Path -LiteralPath $userData) {
    Get-ChildItem -LiteralPath $userData -Directory | ForEach-Object {
        $gameSaves = Join-Path $_.FullName '497780'
        if (Test-Path -LiteralPath $gameSaves) {
            $sources += @{ Source = $gameSaves; Label = ('steam-' + $_.Name) }
        }
    }
}
foreach ($folder in @([Environment]::GetFolderPath('ApplicationData'),[Environment]::GetFolderPath('LocalApplicationData'),(Join-Path $SteamDirectory 'steamapps\common\Recursed'))) {
    if (Test-Path -LiteralPath $folder) {
        Get-ChildItem -LiteralPath $folder -Force | Where-Object { $_.Name -match '^recursed\.(ini|conf|sav)$' -or ($_.PSIsContainer -and $_.Name -ieq 'Recursed') } | ForEach-Object {
            $sources += @{Source=$_.FullName; Label=('local-' + $sources.Count + '-' + $_.Name)}
        }
    }
}
$manifest = @()
foreach ($entry in $sources) {
    $target = Join-Path $backupRoot $entry.Label
    Copy-Item -LiteralPath $entry.Source -Destination $target -Recurse
    $sourceItem = Get-Item -LiteralPath $entry.Source
    $files = if ($sourceItem.PSIsContainer) { @(Get-ChildItem -LiteralPath $entry.Source -File -Recurse -Force) } else { @($sourceItem) }
    foreach ($file in $files) {
        $relative = if ($sourceItem.PSIsContainer) { $file.FullName.Substring($entry.Source.Length).TrimStart('\') } else { '' }
        $copy = if ($relative) { Join-Path $target $relative } else { $target }
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $hash) { throw "Backup mismatch: $($file.FullName)" }
        $manifest += [ordered]@{source=$file.FullName; backup=$copy; bytes=$file.Length; modifiedUtc=$file.LastWriteTimeUtc.ToString('o'); sha256=$hash}
    }
}
if ($manifest.Count -eq 0) { throw 'No Recursed saves found.' }
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backupRoot 'manifest.json') -Encoding utf8
Write-Output "Verified $($manifest.Count) files: $backupRoot"
$manifest | ForEach-Object { [pscustomobject]@{File=(Split-Path -Leaf $_.source);Bytes=$_.bytes;ModifiedUtc=$_.modifiedUtc} }
