param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'
if (!$env:SIGNING_PFX -or !$env:SIGNING_PASSWORD) { throw 'Signing secrets are not configured' }
$certificate = Join-Path $env:RUNNER_TEMP "streamboard-signing-$([Guid]::NewGuid()).pfx"
try {
    [IO.File]::WriteAllBytes($certificate,[Convert]::FromBase64String($env:SIGNING_PFX))
    $signtool = Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/bin/*/x64/signtool.exe' | Sort-Object FullName -Descending | Select-Object -First 1
    if (!$signtool) { throw 'signtool not found' }
    & $signtool.FullName sign /fd SHA256 /f $certificate /p $env:SIGNING_PASSWORD /tr http://timestamp.digicert.com /td SHA256 $Path
    if ($LASTEXITCODE) { throw 'Code signing failed' }
} finally { if (Test-Path -LiteralPath $certificate) { Remove-Item -LiteralPath $certificate -Force } }
