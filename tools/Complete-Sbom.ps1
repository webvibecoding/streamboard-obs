param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$QtRoot
)
$ErrorActionPreference = 'Stop'
$document = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable
if ($document.spdxVersion -ne 'SPDX-2.2') { throw 'Unexpected SPDX version' }
$root = @($document.packages | Where-Object name -eq 'Streamboard.OBS')
if ($root.Count -ne 1 -or $document.packages.Count -ne 1) { throw 'Unexpected scanned dependencies; scan only plugin sources' }
$root[0].licenseDeclared = 'GPL-2.0-or-later'
$root[0].licenseConcluded = 'GPL-2.0-or-later'
$spec = Get-Content buildspec.json -Raw | ConvertFrom-Json
$obsVersion = $spec.dependencies.'obs-studio'.version
$versionLine = Get-Content -LiteralPath (Join-Path $QtRoot 'mkspecs/qconfig.pri') | Where-Object { $_ -match '^\s*QT_VERSION\s*=' } | Select-Object -First 1
if (!$versionLine -or $versionLine -notmatch '=\s*(\d+\.\d+\.\d+)\s*$') { throw 'Could not identify the pinned Qt version' }
$qtVersion = $Matches[1]
$packages = @(
    @{SPDXID='SPDXRef-libobs'; name='libobs'; versionInfo=$obsVersion; licenseDeclared='GPL-2.0-or-later'; downloadLocation="https://github.com/obsproject/obs-studio/releases/tag/$obsVersion"},
    @{SPDXID='SPDXRef-obs-frontend-api'; name='obs-frontend-api'; versionInfo=$obsVersion; licenseDeclared='GPL-2.0-or-later'; downloadLocation="https://github.com/obsproject/obs-studio/releases/tag/$obsVersion"},
    @{SPDXID='SPDXRef-QtBase'; name='Qt Base (Core, Widgets, Network, Concurrent)'; versionInfo=$qtVersion; licenseDeclared='NOASSERTION'; downloadLocation="https://download.qt.io/official_releases/qt/$($qtVersion -replace '\.\d+$','')/$qtVersion/submodules/"}
)
foreach ($package in $packages) {
    $package.filesAnalyzed = $false
    $package.licenseConcluded = 'NOASSERTION'
    $package.copyrightText = 'NOASSERTION'
    $package.description = 'External runtime dependency supplied by the OBS installation, not bundled in this plugin package. Qt binaries used to build this plugin are pinned in buildspec.json.'
    $document.packages += $package
    $document.relationships += @{spdxElementId=$root[0].SPDXID; relatedSpdxElement=$package.SPDXID; relationshipType='DEPENDS_ON'}
}
$document.creationInfo.creators += 'Tool: Streamboard native dependency inventory 1.0.0'
[IO.File]::WriteAllText((Resolve-Path -LiteralPath $Path).Path,($document | ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
Write-Output "SPDX inventory: plugin, libobs $obsVersion, frontend API $obsVersion, Qt $qtVersion; $($document.files.Count) packaged files"
