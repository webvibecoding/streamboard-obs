# Runs only on a disposable CI runner. Never start the user's OBS to test a plugin.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'OBS smoke is restricted to GitHub Actions' }
$root = Join-Path $env:RUNNER_TEMP "streamboard-obs-smoke-$([Guid]::NewGuid())"
New-Item -ItemType Directory -Path $root -Force | Out-Null
$archive = Join-Path $root 'obs.zip'
Invoke-WebRequest 'https://github.com/obsproject/obs-studio/releases/download/33.0.1/OBS-Studio-33.0.1-Windows-x64.zip' -OutFile $archive
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '0d9436451c2d772b33428e3fdae9776f071895c335de17283176306fb72651d3') { throw 'OBS archive checksum mismatch' }
Expand-Archive -LiteralPath $archive -DestinationPath $root
Copy-Item -LiteralPath artifacts/package/plugins -Destination (Join-Path $root 'plugins') -Recurse
New-Item -ItemType File -Path (Join-Path $root 'portable_mode.txt') | Out-Null
$settingsRoot = Join-Path $root 'config/obs-studio'
New-Item -ItemType Directory -Path $settingsRoot -Force | Out-Null
@'
[General]
Pre19Defaults=false
FirstRun=true
LastVersion=553648383
InfoIncrement=1
[BasicWindow]
PreviewEnabled=false
[Video]
Renderer=Direct3D 11
'@ | Set-Content -LiteralPath (Join-Path $settingsRoot 'global.ini') -Encoding utf8
$env:STREAMBOARD_OBS_CI_SMOKE = '1'
$process = $null
try {
    $executable = Join-Path $root 'bin/64bit/obs64.exe'
    $process = Start-Process -FilePath $executable -WorkingDirectory (Split-Path $executable) -ArgumentList '--portable','--multi','--disable-updater','--disable-shutdown-check','--disable-missing-files-check','--verbose' -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(90)
    $passed = $false
    do {
        Start-Sleep -Seconds 2
        $process.Refresh()
        $logs = Get-ChildItem (Join-Path $settingsRoot 'logs') -Filter '*.txt' -ErrorAction SilentlyContinue
        $content = ($logs | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
        if ($content -match '\[Streamboard OBS\] CI dock check passed') { $passed = $true; break }
        if ($process.HasExited) { throw "OBS exited before dock check: $($process.ExitCode)" }
    } while ([DateTime]::UtcNow -lt $deadline)
    if (!$passed) { throw 'OBS dock smoke timed out' }
    $process.Refresh()
    if (!$process.CloseMainWindow()) { throw 'Could not request graceful OBS shutdown' }
    if (!$process.WaitForExit(30000)) { throw 'OBS did not exit cleanly' }
    if ($process.ExitCode -ne 0) { throw "OBS exited with $($process.ExitCode)" }
    $content = ($logs | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
    if ($content -notmatch '\[Streamboard OBS\] shutdown complete') { throw 'Plugin shutdown callback missing' }
} finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
    New-Item -ItemType Directory -Path artifacts/smoke-logs -Force | Out-Null
    Get-ChildItem (Join-Path $settingsRoot 'logs') -Filter '*.txt' -ErrorAction SilentlyContinue | Copy-Item -Destination artifacts/smoke-logs
    Remove-Item Env:STREAMBOARD_OBS_CI_SMOKE -ErrorAction SilentlyContinue
}
