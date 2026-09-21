$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vendorPath = Join-Path $projectRoot 'vendor'
$archive = Join-Path $vendorPath 'lua-5.2.4.tar.gz'
New-Item -ItemType Directory -Path $vendorPath -Force | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri 'https://www.lua.org/ftp/lua-5.2.4.tar.gz' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne 'B9E2E4AAD6789B3B63A056D442F7B39F0ECFCA3AE0F1FC0AE4E9614401B69F4B') {
    throw 'Lua archive hash mismatch.'
}
if (!(Test-Path -LiteralPath (Join-Path $vendorPath 'lua-5.2.4\src\lua.h'))) {
    tar -xzf $archive -C $vendorPath
    if ($LASTEXITCODE -ne 0) { throw 'Lua extraction failed.' }
}
Write-Output 'Verified Lua 5.2.4 sources are ready.'
