[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("sections", "find-ascii", "find-va-refs")]
    [string]$Command,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$Path,

    [Parameter(Position = 2)]
    [string]$Value
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-U16([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt16($Bytes, $Offset)
}

function Read-U32([byte[]]$Bytes, [int]$Offset) {
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Get-AsciiName([byte[]]$Bytes, [int]$Offset, [int]$Length) {
    $slice = $Bytes[$Offset..($Offset + $Length - 1)]
    $text = [System.Text.Encoding]::ASCII.GetString($slice)
    return $text.Trim([char]0)
}

function Get-PeInfo {
    param([byte[]]$Bytes)

    if ((Read-U16 $Bytes 0) -ne 0x5A4D) {
        throw "Not a PE file"
    }

    $peOffset = [int](Read-U32 $Bytes 0x3C)
    if ((Read-U32 $Bytes $peOffset) -ne 0x4550) {
        throw "Invalid PE signature"
    }

    $fileHeaderOffset = $peOffset + 4
    $numberOfSections = [int](Read-U16 $Bytes ($fileHeaderOffset + 2))
    $sizeOfOptionalHeader = [int](Read-U16 $Bytes ($fileHeaderOffset + 16))
    $optionalHeaderOffset = $fileHeaderOffset + 20
    $magic = Read-U16 $Bytes $optionalHeaderOffset
    if ($magic -ne 0x10B) {
        throw "Only PE32 is supported by this helper"
    }

    $imageBase = Read-U32 $Bytes ($optionalHeaderOffset + 28)
    $sectionTableOffset = $optionalHeaderOffset + $sizeOfOptionalHeader

    $sections = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $numberOfSections; $i++) {
        $offset = $sectionTableOffset + ($i * 40)
        $name = Get-AsciiName $Bytes $offset 8
        $virtualSize = Read-U32 $Bytes ($offset + 8)
        $virtualAddress = Read-U32 $Bytes ($offset + 12)
        $rawSize = Read-U32 $Bytes ($offset + 16)
        $rawPointer = Read-U32 $Bytes ($offset + 20)
        $sections.Add([pscustomobject]@{
            Name = $name
            VirtualSize = $virtualSize
            VirtualAddress = $virtualAddress
            RawSize = $rawSize
            RawPointer = $rawPointer
        })
    }

    return [pscustomobject]@{
        ImageBase = $imageBase
        Sections = $sections
    }
}

function Convert-FileOffsetToVa {
    param(
        [object]$PeInfo,
        [uint32]$FileOffset
    )

    foreach ($section in $PeInfo.Sections) {
        $start = [uint32]$section.RawPointer
        $size = [Math]::Max([uint32]$section.RawSize, [uint32]$section.VirtualSize)
        $end = $start + $size
        if ($FileOffset -ge $start -and $FileOffset -lt $end) {
            $rva = [uint32]($section.VirtualAddress + ($FileOffset - $start))
            $va = [uint32]($PeInfo.ImageBase + $rva)
            return [pscustomobject]@{
                Section = $section.Name
                Rva = $rva
                Va = $va
            }
        }
    }

    return $null
}

function Find-BytePattern {
    param(
        [byte[]]$Bytes,
        [byte[]]$Pattern
    )

    $results = New-Object System.Collections.Generic.List[uint32]
    if ($Pattern.Length -eq 0 -or $Bytes.Length -lt $Pattern.Length) {
        return $results
    }

    for ($i = 0; $i -le $Bytes.Length - $Pattern.Length; $i++) {
        $matched = $true
        for ($j = 0; $j -lt $Pattern.Length; $j++) {
            if ($Bytes[$i + $j] -ne $Pattern[$j]) {
                $matched = $false
                break
            }
        }

        if ($matched) {
            $results.Add([uint32]$i)
        }
    }

    return $results
}

$bytes = [System.IO.File]::ReadAllBytes($Path)
$peInfo = Get-PeInfo $bytes

switch ($Command) {
    "sections" {
        "ImageBase=0x{0:x8}" -f $peInfo.ImageBase
        foreach ($section in $peInfo.Sections) {
            "{0} VA=0x{1:x8} VS=0x{2:x8} RAW=0x{3:x8} SIZE=0x{4:x8}" -f `
                $section.Name, $section.VirtualAddress, $section.VirtualSize, $section.RawPointer, $section.RawSize
        }
    }

    "find-ascii" {
        if (-not $Value) {
            throw "Value is required for find-ascii"
        }

        $pattern = [System.Text.Encoding]::ASCII.GetBytes($Value)
        foreach ($offset in (Find-BytePattern -Bytes $bytes -Pattern $pattern)) {
            $mapped = Convert-FileOffsetToVa -PeInfo $peInfo -FileOffset $offset
            if ($mapped -ne $null) {
                "file=0x{0:x8} section={1} rva=0x{2:x8} va=0x{3:x8} text={4}" -f `
                    $offset, $mapped.Section, $mapped.Rva, $mapped.Va, $Value
            } else {
                "file=0x{0:x8} section=(none) text={1}" -f $offset, $Value
            }
        }
    }

    "find-va-refs" {
        if (-not $Value) {
            throw "Value is required for find-va-refs"
        }

        $trimmed = $Value.Trim()
        if ($trimmed.StartsWith("0x")) {
            $trimmed = $trimmed.Substring(2)
        }

        $va = [uint32]::Parse($trimmed, [System.Globalization.NumberStyles]::HexNumber)
        $pattern = [BitConverter]::GetBytes($va)
        foreach ($offset in (Find-BytePattern -Bytes $bytes -Pattern $pattern)) {
            $mapped = Convert-FileOffsetToVa -PeInfo $peInfo -FileOffset $offset
            if ($mapped -ne $null) {
                "ref_file=0x{0:x8} ref_section={1} ref_rva=0x{2:x8} ref_va=0x{3:x8} target_va=0x{4:x8}" -f `
                    $offset, $mapped.Section, $mapped.Rva, $mapped.Va, $va
            } else {
                "ref_file=0x{0:x8} ref_section=(none) target_va=0x{1:x8}" -f $offset, $va
            }
        }
    }
}
