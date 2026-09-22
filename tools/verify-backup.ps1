param([string]$Manifest)
$ErrorActionPreference = 'Stop'
if (!$Manifest) {
    $projectRoot = Split-Path -Parent $PSScriptRoot
    $latest = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'backups') -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (!$latest) { throw 'No backup found.' }
    $Manifest = Join-Path $latest.FullName 'manifest.json'
}
# Windows PowerShell hands a JSON array down the pipeline as one object, so wrapping the
# pipeline itself produces a single nested array and the loop below would verify nothing.
$parsed = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$entries = @($parsed)
foreach ($entry in $entries) {
    if ((Get-FileHash -LiteralPath $entry.backup -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Backup damaged: $($entry.backup)" }
    if ((Get-FileHash -LiteralPath $entry.source -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Original changed since backup: $($entry.source)" }
}
Write-Output "PASS: $($entries.Count) backups verified; originals are unchanged."
