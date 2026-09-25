param([string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Recursed')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runtimePath = Join-Path $projectRoot 'runtime'
$gameExe = Join-Path $GameDirectory 'Recursed.exe'
$expectedHash = '0E47D5DF0F45152978777E5F8EC7F2435BAB79CD84D630CFAAD5106B90D74251'
if ((Get-FileHash -LiteralPath $gameExe).Hash -ne $expectedHash) { throw 'Unsupported Recursed executable.' }
if (!(Test-Path -LiteralPath $runtimePath)) {
    New-Item -ItemType Directory -Path $runtimePath | Out-Null
    Get-ChildItem -LiteralPath $GameDirectory | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $runtimePath -Recurse }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'tests\peek-lab.lua') -Destination (Join-Path $runtimePath 'custom\missions\peek-lab.lua')
Copy-Item -LiteralPath (Join-Path $projectRoot 'tests\state-lab.lua') -Destination (Join-Path $runtimePath 'custom\missions\state-lab.lua')
Copy-Item -LiteralPath (Join-Path $projectRoot 'tests\render-lab.lua') -Destination (Join-Path $runtimePath 'custom\missions\render-lab.lua')
Copy-Item -LiteralPath (Join-Path $projectRoot 'tests\paradox-lab.lua') -Destination (Join-Path $runtimePath 'custom\missions\paradox-lab.lua')
Copy-Item -LiteralPath (Join-Path $projectRoot 'tests\nexus.lua') -Destination (Join-Path $runtimePath 'data\nexus.lua')
Write-Output "Isolated runtime ready: $runtimePath"
