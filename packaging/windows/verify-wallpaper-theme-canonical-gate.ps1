[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$LibraryIni,

    [Parameter(Mandatory = $true)]
    [string]$AssignmentsIni,

    [Parameter(Mandatory = $false)]
    [string]$CanonicalNeonPackage,

    [Parameter(Mandatory = $false)]
    [string]$OperationScript,

    [Parameter(Mandatory = $false)]
    [string[]]$OperationArgument = @()
)

$ErrorActionPreference = 'Stop'

function Read-RawBytes([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file does not exist: $Path"
    }
    return [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path))
}

function Assert-ByteIdentical([byte[]]$Before, [byte[]]$After, [string]$Label) {
    if ($Before.Length -ne $After.Length) {
        throw "$Label changed length ($($Before.Length) -> $($After.Length))."
    }
    for ($i = 0; $i -lt $Before.Length; $i++) {
        if ($Before[$i] -ne $After[$i]) {
            throw "$Label changed at byte offset $i."
        }
    }
}

function Assert-Utf16LeBom([string]$Path, [string]$Label) {
    $bytes = Read-RawBytes $Path
    if ($bytes.Length -lt 2 -or $bytes[0] -ne 0xFF -or $bytes[1] -ne 0xFE) {
        throw "$Label is not UTF-16LE with BOM: $Path"
    }
}

function Assert-NoLegacyManagedRows([string]$Path) {
    Assert-Utf16LeBom $Path 'Wallpaper library'
    $text = [System.Text.Encoding]::Unicode.GetString((Read-RawBytes $Path), 2, (Read-RawBytes $Path).Length - 2)
    foreach ($legacyId in @('scene-aurora', 'scene-neon', 'scene-grid')) {
        if ($text -match [regex]::Escape("[Item.$legacyId]")) {
            throw "Canonical-only Library gate failed: visible legacy row still exists for $legacyId."
        }
    }
}

Assert-Utf16LeBom $LibraryIni 'Wallpaper library'
Assert-Utf16LeBom $AssignmentsIni 'Monitor assignments'
Assert-NoLegacyManagedRows $LibraryIni

if ($CanonicalNeonPackage) {
    $manifest = Join-Path $CanonicalNeonPackage 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "Canonical Neon City manifest is missing: $manifest"
    }
    $json = Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($json.id -ne 'com.goodloong.miaodesk.theme.neon-city') {
        throw "Unexpected canonical manifest id: $($json.id)"
    }
    if ($json.kind -ne 'wallpaper' -or $json.runtime -notin @('scene', 'web')) {
        throw 'Canonical wallpaper manifest kind/runtime contract failed.'
    }
}

$beforeLibrary = Read-RawBytes $LibraryIni
$beforeAssignments = Read-RawBytes $AssignmentsIni

if ($OperationScript) {
    if (-not (Test-Path -LiteralPath $OperationScript -PathType Leaf)) {
        throw "Operation script does not exist: $OperationScript"
    }
    & pwsh -NoLogo -NoProfile -NonInteractive -File $OperationScript @OperationArgument
    if ($LASTEXITCODE -ne 0) {
        throw "Operation script failed with exit code $LASTEXITCODE."
    }
}

$afterLibrary = Read-RawBytes $LibraryIni
$afterAssignments = Read-RawBytes $AssignmentsIni
Assert-ByteIdentical $beforeAssignments $afterAssignments 'monitor-assignments.ini'
Assert-Utf16LeBom $LibraryIni 'Wallpaper library after operation'
Assert-Utf16LeBom $AssignmentsIni 'Monitor assignments after operation'
Assert-NoLegacyManagedRows $LibraryIni

Write-Host 'Wallpaper canonical identity gate passed.'
Write-Host 'Verified: UTF-16LE library/assignment files, canonical-only shipped built-in rows, and no assignment byte rewrite.'
