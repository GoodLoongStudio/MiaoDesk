param(
    [Parameter(Mandatory = $true)][string]$Root,
    [int]$MaxInstallRootChars = 85,
    [int]$MaxProjectedPathChars = 248,
    [int]$MaxProductDepth = 6
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path $Root -PathType Container)) {
    throw "Package root does not exist: $Root"
}

$resolvedRoot = [IO.Path]::GetFullPath((Resolve-Path $Root).Path).TrimEnd('\')
$items = @(Get-ChildItem $resolvedRoot -Recurse -Force -File | ForEach-Object {
    $relative = $_.FullName.Substring($resolvedRoot.Length).TrimStart('\')
    $segments = @($relative -split '[\\/]' | Where-Object { $_ -ne '' })
    $depth = [Math]::Max(0, $segments.Count - 1)
    $projected = $MaxInstallRootChars + 1 + $relative.Length
    [pscustomobject]@{
        Relative = $relative
        RelativeLength = $relative.Length
        Depth = $depth
        ProjectedLength = $projected
    }
})

if ($items.Count -eq 0) { throw "Package root is empty: $resolvedRoot" }

$longest = $items | Sort-Object ProjectedLength -Descending | Select-Object -First 12
Write-Host "Path budget: assume install root <= $MaxInstallRootChars chars; legacy-safe projected limit=$MaxProjectedPathChars" -ForegroundColor Cyan
$longest | Format-Table ProjectedLength,Depth,Relative -AutoSize | Out-Host

$tooLong = @($items | Where-Object { $_.ProjectedLength -gt $MaxProjectedPathChars })
if ($tooLong.Count -gt 0) {
    foreach ($item in @($tooLong | Sort-Object ProjectedLength -Descending | Select-Object -First 30)) {
        Write-Host ("OVER {0}: {1}" -f $item.ProjectedLength, $item.Relative) -ForegroundColor Red
    }
    throw "Package contains $($tooLong.Count) path(s) that exceed the stock-Windows path budget. Change the runtime/package layout; do not require LongPathsEnabled."
}

# Product-owned files stay shallow. Runtime V3 owns its third-party dependency
# graph below AI, so that tree is governed by the stricter projected
# MAX_PATH gate above rather than the product-content nesting gate.
$productOwned = @($items | Where-Object {
    $_.Relative -notlike 'Runtime\*' -and
    $_.Relative -notlike 'AI\*' -and
    $_.Relative -notlike 'Pi\*' -and
    $_.Relative -notlike 'Goz\*'
})
$tooDeep = @($productOwned | Where-Object { $_.Depth -gt $MaxProductDepth })
if ($tooDeep.Count -gt 0) {
    $tooDeep | Sort-Object Depth -Descending | Select-Object -First 30 |
        Format-Table Depth,Relative -AutoSize | Out-Host
    throw "CMake-owned install content exceeds the $MaxProductDepth-level nesting budget."
}

Write-Host "Package path budget passed: $($items.Count) files checked without relying on Windows long-path policy." -ForegroundColor Green
