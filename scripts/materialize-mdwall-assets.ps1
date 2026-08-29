param(
    [Parameter(Mandatory=$true)][string]$SourceDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $SourceDirectory -PathType Container)) {
    throw "mdwall source directory not found: $SourceDirectory"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$assetOutput = Join-Path $OutputDirectory 'assets'
New-Item -ItemType Directory -Force -Path $assetOutput | Out-Null

foreach ($name in @('manifest.json', 'scene.ini', 'README.md')) {
    $source = Join-Path $SourceDirectory $name
    if (Test-Path $source -PathType Leaf) {
        Copy-Item $source (Join-Path $OutputDirectory $name) -Force
    }
}

$partsRoot = Join-Path $SourceDirectory 'asset-parts'
if (-not (Test-Path $partsRoot -PathType Container)) {
    throw "mdwall asset-parts directory not found: $partsRoot"
}

$groups = Get-ChildItem $partsRoot -File | ForEach-Object {
    if ($_.Name -match '^(?<asset>.+)\.part(?<index>\d+)$') {
        [pscustomobject]@{ Asset=$Matches.asset; Index=[int]$Matches.index; File=$_ }
    }
} | Group-Object Asset

if (-not $groups) { throw 'No mdwall asset parts were found.' }

foreach ($group in $groups) {
    $destination = Join-Path $assetOutput $group.Name
    $stream = [System.IO.File]::Open($destination, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    try {
        foreach ($part in ($group.Group | Sort-Object Index)) {
            $input = [System.IO.File]::OpenRead($part.File.FullName)
            try { $input.CopyTo($stream) } finally { $input.Dispose() }
        }
    } finally {
        $stream.Dispose()
    }
    if ((Get-Item $destination).Length -le 0) { throw "Materialized asset is empty: $destination" }
}

Write-Host "Materialized mdwall: $OutputDirectory"
