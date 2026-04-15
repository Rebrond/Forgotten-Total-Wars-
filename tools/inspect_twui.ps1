[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("strings", "context")]
    [string]$Command,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$Path,

    [Parameter(Position = 2)]
    [string]$Needle,

    [string]$Pattern,
    [int]$MinLen = 4,
    [int]$Limit = 200,
    [int]$Radius = 128,
    [int]$DumpSize = 384
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-AsciiStrings {
    param(
        [byte[]]$Bytes,
        [int]$MinLength
    )

    $results = New-Object System.Collections.Generic.List[object]
    $sb = New-Object System.Text.StringBuilder
    $start = -1

    for ($i = 0; $i -lt $Bytes.Length; $i++) {
        $b = $Bytes[$i]
        if ($b -ge 32 -and $b -le 126) {
            if ($start -lt 0) {
                $start = $i
            }
            [void]$sb.Append([char]$b)
        } else {
            if ($sb.Length -ge $MinLength) {
                $results.Add([pscustomobject]@{
                    Offset = $start
                    Text = $sb.ToString()
                })
            }
            $start = -1
            $sb.Clear() | Out-Null
        }
    }

    if ($sb.Length -ge $MinLength) {
        $results.Add([pscustomobject]@{
            Offset = $start
            Text = $sb.ToString()
        })
    }

    return $results
}

function Get-Utf16Strings {
    param(
        [byte[]]$Bytes,
        [int]$MinLength
    )

    $results = New-Object System.Collections.Generic.List[object]
    $sb = New-Object System.Text.StringBuilder
    $start = -1

    for ($i = 0; $i + 1 -lt $Bytes.Length; $i += 2) {
        $lo = $Bytes[$i]
        $hi = $Bytes[$i + 1]
        if ($hi -eq 0 -and $lo -ge 32 -and $lo -le 126) {
            if ($start -lt 0) {
                $start = $i
            }
            [void]$sb.Append([char]$lo)
        } else {
            if ($sb.Length -ge $MinLength) {
                $results.Add([pscustomobject]@{
                    Offset = $start
                    Text = $sb.ToString()
                })
            }
            $start = -1
            $sb.Clear() | Out-Null
        }
    }

    if ($sb.Length -ge $MinLength) {
        $results.Add([pscustomobject]@{
            Offset = $start
            Text = $sb.ToString()
        })
    }

    return $results
}

function Get-NearbyInts {
    param(
        [byte[]]$Bytes,
        [int]$Center,
        [int]$Range
    )

    $results = New-Object System.Collections.Generic.List[object]
    $start = [Math]::Max(0, $Center - $Range)
    $end = [Math]::Min($Bytes.Length - 4, $Center + $Range)

    for ($offset = $start; $offset -le $end; $offset += 4) {
        $value = [BitConverter]::ToUInt32($Bytes, $offset)
        if ($value -eq 0 -or $value -eq 0xFFFFFFFF) {
            continue
        }
        if ($value -le 4096) {
            $results.Add([pscustomobject]@{
                Offset = $offset
                Value = $value
            })
        }
    }

    return $results
}

function Write-HexDump {
    param(
        [byte[]]$Bytes,
        [int]$Start,
        [int]$Count
    )

    $safeStart = [Math]::Max(0, $Start)
    $safeEnd = [Math]::Min($Bytes.Length, $safeStart + $Count)
    for ($row = $safeStart; $row -lt $safeEnd; $row += 16) {
        $chunkEnd = [Math]::Min($safeEnd, $row + 16)
        $slice = $Bytes[$row..($chunkEnd - 1)]
        $hex = ($slice | ForEach-Object { $_.ToString("x2") }) -join " "
        $ascii = ($slice | ForEach-Object { if ($_ -ge 32 -and $_ -le 126) { [char]$_ } else { "." } }) -join ""
        "{0:x8}  {1,-47}  {2}" -f $row, $hex, $ascii
    }
}

$bytes = [System.IO.File]::ReadAllBytes($Path)

switch ($Command) {
    "strings" {
        $hits = @(Get-AsciiStrings -Bytes $bytes -MinLength $MinLen) + @(Get-Utf16Strings -Bytes $bytes -MinLength $MinLen)
        $hits = $hits | Sort-Object Offset
        if ($Pattern) {
            $hits = $hits | Where-Object { $_.Text -like "*$Pattern*" }
        }
        $hits | Select-Object -First $Limit | ForEach-Object {
            "{0:x8}  {1}" -f $_.Offset, $_.Text
        }
    }
    "context" {
        if (-not $Needle) {
            throw "Needle is required for context"
        }

        $text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
        $index = $text.IndexOf($Needle)
        if ($index -lt 0) {
            throw "Needle not found: $Needle"
        }

        "needle='{0}' offset=0x{1:x8}" -f $Needle, $index
        ""
        "Nearby small u32 values:"
        Get-NearbyInts -Bytes $bytes -Center $index -Range $Radius | ForEach-Object {
            "  0x{0:x8}  {1}" -f $_.Offset, $_.Value
        }
        ""
        Write-HexDump -Bytes $bytes -Start ($index - $Radius) -Count $DumpSize
    }
}
