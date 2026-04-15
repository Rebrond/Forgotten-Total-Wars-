[CmdletBinding()]
param(
    [ValidateSet("conservative", "balanced", "aggressive")]
    [string]$Preset = "conservative",

    [int]$Bump = 0,

    [string]$FontRoot = "",

    [string]$OutputRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $PSCommandPath
if (-not $FontRoot) {
    $FontRoot = Join-Path $scriptRoot "..\\font"
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $scriptRoot "..\\mod_staging\\font_overrides"
}

function Get-EffectiveBump {
    param(
        [string]$SelectedPreset,
        [int]$ExplicitBump
    )

    if ($ExplicitBump -gt 0) {
        if ($ExplicitBump -lt 1) {
            throw "Bump must be greater than zero."
        }
        return $ExplicitBump
    }

    switch ($SelectedPreset) {
        "conservative" { return 2 }
        "balanced"     { return 4 }
        "aggressive"   { return 6 }
        default        { throw "Unsupported preset: $SelectedPreset" }
    }
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

function Get-FontInfo {
    param(
        [System.IO.FileInfo]$File,
        [string]$RootPath
    )

    if ($File.Name -notmatch '^(?<Family>.+_)(?<Size>\d+)\.cuf$') {
        return $null
    }

    $relativePath = Get-RelativePath -BasePath $RootPath -Path $File.FullName
    $relativeDir = Split-Path -Parent $relativePath
    if (-not $relativeDir) {
        $relativeDir = "."
    }

    return [pscustomobject]@{
        FullName    = $File.FullName
        Name        = $File.Name
        RelativeDir = $relativeDir
        Family      = $Matches["Family"]
        Size        = [int]$Matches["Size"]
    }
}

$fontRootPath = (Resolve-Path -LiteralPath $FontRoot).Path
$outputRootPath = [System.IO.Path]::GetFullPath($OutputRoot)
$sizeBump = Get-EffectiveBump -SelectedPreset $Preset -ExplicitBump $Bump

if (Test-Path -LiteralPath $outputRootPath) {
    Remove-Item -LiteralPath $outputRootPath -Recurse -Force
}

New-Item -ItemType Directory -Path $outputRootPath | Out-Null

$fontFiles = Get-ChildItem -LiteralPath $fontRootPath -Recurse -File -Filter *.cuf |
    ForEach-Object { Get-FontInfo -File $_ -RootPath $fontRootPath } |
    Where-Object { $_ -ne $null }

if (-not $fontFiles) {
    throw "No .cuf fonts found under $fontRootPath"
}

$manifestLines = New-Object System.Collections.Generic.List[string]
$manifestLines.Add("# Shogun 2 font override manifest")
$manifestLines.Add("preset=$Preset")
$manifestLines.Add("bump=$sizeBump")
$manifestLines.Add("")

$fontGroups = $fontFiles | Group-Object RelativeDir, Family

foreach ($group in $fontGroups) {
    $groupFonts = $group.Group | Sort-Object Size

    foreach ($targetFont in $groupFonts) {
        $desiredSize = $targetFont.Size + $sizeBump
        $sourceFont = $groupFonts |
            Where-Object { $_.Size -ge $desiredSize } |
            Sort-Object Size |
            Select-Object -First 1

        if (-not $sourceFont) {
            $sourceFont = $groupFonts |
                Sort-Object Size -Descending |
                Select-Object -First 1
        }

        if ($sourceFont.Size -eq $targetFont.Size) {
            continue
        }

        $targetDir = if ($targetFont.RelativeDir -eq ".") {
            $outputRootPath
        } else {
            Join-Path $outputRootPath $targetFont.RelativeDir
        }

        if (-not (Test-Path -LiteralPath $targetDir)) {
            New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
        }

        $targetPath = Join-Path $targetDir $targetFont.Name
        Copy-Item -LiteralPath $sourceFont.FullName -Destination $targetPath -Force

        $sourceRelative = if ($sourceFont.RelativeDir -eq ".") {
            $sourceFont.Name
        } else {
            Join-Path $sourceFont.RelativeDir $sourceFont.Name
        }

        $targetRelative = if ($targetFont.RelativeDir -eq ".") {
            $targetFont.Name
        } else {
            Join-Path $targetFont.RelativeDir $targetFont.Name
        }

        $manifestLines.Add("$targetRelative <- $sourceRelative")
    }
}

$manifestPath = Join-Path $outputRootPath "font_override_manifest.txt"
[System.IO.File]::WriteAllLines($manifestPath, $manifestLines)

"Created font overrides in: $outputRootPath"
"Preset: $Preset"
"Bump: +$sizeBump"
"Files staged: $($manifestLines.Count - 4)"
