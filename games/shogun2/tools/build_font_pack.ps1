[CmdletBinding()]
param(
    [string]$PackRoot = "",

    [string]$PackPath = "",

    [string]$PfmPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $PSCommandPath
if (-not $PackRoot) {
    $PackRoot = Join-Path $scriptRoot "..\\mod_staging\\pack_root"
}
if (-not $PackPath) {
    $PackPath = Join-Path $scriptRoot "..\\mod_staging\\shogun2_ui_font_conservative.pack"
}
if (-not $PfmPath) {
    $PfmPath = Join-Path $scriptRoot "..\\Pack File Manager 5.2.4\\pfm.exe"
}

function Get-RelativePath {
    param(
        [string]$BasePath,
        [string]$Path
    )

    $baseUri = [System.Uri]((Resolve-Path -LiteralPath $BasePath).Path.TrimEnd('\') + '\')
    $pathUri = [System.Uri](Resolve-Path -LiteralPath $Path).Path
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()).Replace('/', '\')
}

$packRootPath = (Resolve-Path -LiteralPath $PackRoot).Path
$packPathFull = [System.IO.Path]::GetFullPath($PackPath)
$pfmPathFull = (Resolve-Path -LiteralPath $PfmPath).Path

$files = Get-ChildItem -LiteralPath $packRootPath -Recurse -File |
    Where-Object { $_.Name -ne "font_override_manifest.txt" } |
    ForEach-Object { Get-RelativePath -BasePath $packRootPath -Path $_.FullName }

if (-not $files) {
    throw "No packable files found under $packRootPath"
}

Push-Location $packRootPath
try {
    & $pfmPathFull c $packPathFull @files
    if ($LASTEXITCODE -ne 0) {
        throw "pfm create failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

"Created pack: $packPathFull"
"Files packed: $($files.Count)"
