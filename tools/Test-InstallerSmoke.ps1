# Real installer/file-lock regression coverage, exclusively on a disposable CI runner.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Installer smoke is restricted to GitHub Actions' }
$version = (Get-Content buildspec.json -Raw | ConvertFrom-Json).version
$installer = (Resolve-Path -LiteralPath "artifacts/Streamboard.OBS-Setup-$version.exe").Path
$fixture = (Resolve-Path -LiteralPath 'build_x64/Release/obs.exe').Path
$root = Join-Path $env:RUNNER_TEMP "Streamboard installer smoke $([Guid]::NewGuid())"
$destination = Join-Path $root 'installed plugin'
$logs = Join-Path $pwd 'artifacts/installer-logs'
New-Item -ItemType Directory -Path $root,$logs -Force | Out-Null
$target = Join-Path $destination 'streamboard-obs.dll'
$payload = (Resolve-Path -LiteralPath 'artifacts/package/plugins/streamboard-obs/streamboard-obs.dll').Path
$expectedHash = (Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash
function Run-InstallerProcess([string]$Executable,[string[]]$Arguments) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(45000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw 'Installer timed out; see installer-logs'
    }
    return $process.ExitCode
}
function Run-Setup([string]$LogName) {
    $log = Join-Path $logs $LogName
    return Run-InstallerProcess $installer @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',"/DIR=`"$destination`"","/LOG=`"$log`"")
}
$sentinel = $null
$lock = $null
try {
    $sentinel = Start-Process -FilePath $fixture -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 200
    if ($sentinel.HasExited) { throw 'Process fixture did not start' }
    if ((Run-Setup 'install.log') -ne 0) { throw 'Install was blocked by an unrelated obs.exe process' }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Installed DLL differs from package' }
    if ($sentinel.HasExited) { throw 'Installer closed the unrelated process' }
    Write-Output 'PASS fresh installation while unrelated obs.exe is running'

    [IO.File]::WriteAllText($target,'Installer regression: locked existing plugin')
    $lockedHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $lock = [IO.File]::Open($target,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    if ((Run-Setup 'locked-dll.log') -eq 0) { throw 'Installer reported success while the destination DLL was locked' }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $lockedHash) { throw 'Installer modified the locked DLL' }
    $lock.Dispose(); $lock = $null
    Write-Output 'PASS actually locked DLL is preserved and installation cannot complete'

    if ((Run-Setup 'repair.log') -ne 0) { throw 'Retry after releasing DLL lock failed' }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expectedHash) { throw 'Retry did not restore the package DLL' }
    Write-Output 'PASS installation succeeds after releasing the file lock'

    $uninstaller = Join-Path $destination 'unins000.exe'
    $uninstallLog = Join-Path $logs 'uninstall.log'
    if ((Run-InstallerProcess $uninstaller @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',"/LOG=`"$uninstallLog`"")) -ne 0) { throw 'Uninstall failed' }
    if (Test-Path -LiteralPath $target) { throw 'Uninstall left the plugin DLL behind' }
    if ($sentinel.HasExited) { throw 'Uninstaller closed the unrelated process' }
    Write-Output 'PASS uninstall removes plugin without closing unrelated obs.exe'
} finally {
    if ($lock) { $lock.Dispose() }
    if ($sentinel -and !$sentinel.HasExited) { $sentinel.Kill(); $sentinel.WaitForExit() }
}
