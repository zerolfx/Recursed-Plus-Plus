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
    Write-Output "PASS: $($tracked.Count) tracked files checked for excluded files and Chinese text."
} finally {
    Pop-Location
}
