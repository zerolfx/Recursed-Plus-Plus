$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $tracked = @(git ls-files)
    if ($LASTEXITCODE -ne 0 -or !$tracked.Count) { throw 'No tracked files found.' }
    foreach ($file in $tracked) {
        if ($file -match '^(runtime|backups|vendor|build|analysis|dist)/' -or $file -match '\.(exe|dll|pdb|obj|log)$') {
            throw "Private, proprietary, or generated file must not be tracked: $file"
        }
        if ($file -match '[\u3400-\u9fff\uf900-\ufaff]') { throw "Non-English filename: $file" }
        if ($file -match '\.(png|gif)$') { continue }
        $body = [IO.File]::ReadAllText((Join-Path $projectRoot $file))
        if ($body -match '[\u3400-\u9fff\uf900-\ufaff]') { throw "Chinese text found in repository file: $file" }
    }
    # What separates the two downloads is easy to undo by accident, so the rules are checked here
    # rather than trusted: the launcher must carry the mod instead of loading a neighbour's, the
    # notices must travel with it, the developer diagnostics must stay behind their flag, and the
    # README must not send a player after files only the developer bundle has.
    $rc = [IO.File]::ReadAllText((Join-Path $projectRoot 'src/launcher.rc'))
    foreach ($payload in 'recursed_peek.dll', 'THIRD_PARTY_NOTICES.md') {
        if ($rc -notmatch [regex]::Escape($payload)) { throw "src/launcher.rc no longer embeds $payload, so the single executable is incomplete." }
    }
    # The resource names are the only thing tying the script that stores the payloads to the code
    # that reads them back, and a rename on one side fails at run time, not at build time.
    $unpacker = [IO.File]::ReadAllText((Join-Path $projectRoot 'src/embedded_plugin.cpp'))
    foreach ($name in ([regex]::Matches($rc, '(?m)^(RECURSEDPEEK\w+)\s+RCDATA') | ForEach-Object { $_.Groups[1].Value })) {
        if ($unpacker -notmatch [regex]::Escape($name)) { throw "src/launcher.rc stores $name, which src/embedded_plugin.cpp never looks for." }
    }
    $gui = [IO.File]::ReadAllText((Join-Path $projectRoot 'src/gui_launcher.cpp'))
    if ($gui -match 'L"recursed_peek\.dll"') { throw 'src/gui_launcher.cpp names a DLL beside the launcher; it must run the copy embedded in the executable.' }
    $plugin = [IO.File]::ReadAllText((Join-Path $projectRoot 'src/plugin.cpp'))
    foreach ($line in ($plugin -split "`n" | Where-Object { $_ -match 'queuedNative\s*=\s*true|Rewind::Verify' })) {
        if ($line -notmatch 'developerMode') { throw "A developer diagnostic is reachable without the developer flag: $($line.Trim())" }
    }
    $readme = [IO.File]::ReadAllText((Join-Path $projectRoot 'README.md'))
    foreach ($developerOnly in 'Start-Preview\.cmd', 'prepare-runtime\.ps1', 'backup-saves\.ps1', 'build\.cmd', 'recursed_peek', 'RECURSED_PEEK_', 'Preview Lab') {
        if ($readme -match $developerOnly) { throw "README.md points a player at something only the developer bundle has: $developerOnly" }
    }
    Write-Output "PASS: $($tracked.Count) tracked files checked for excluded files and Chinese text, and the player download is separated from the developer one."
} finally {
    Pop-Location
}
