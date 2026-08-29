$ErrorActionPreference = 'Stop'

function Test-ReparsePoint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return ((Get-Item -LiteralPath $Path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
}

function Remove-Junction([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    if (-not (Test-ReparsePoint $Path)) { throw "Refusing to remove non-junction path: $Path" }
    [IO.Directory]::Delete($Path, $false)
}

function Remove-DeploymentTree([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    foreach ($relative in @('Runtime', 'Pi', 'Goz')) {
        $child = Join-Path $Path $relative
        if (Test-ReparsePoint $child) { Remove-Junction $child }
    }
    Remove-Item -LiteralPath $Path -Recurse -Force -Confirm:$false -ErrorAction Stop
}

$root = Join-Path $env:TEMP ('td-junction-regression-' + [guid]::NewGuid().ToString('N'))
$store = Join-Path $root 'RuntimeBundle'
$staging = Join-Path $root 'NativeTest.next'

try {
    New-Item -ItemType Directory -Force -Path $store,$staging | Out-Null
    foreach ($relative in @('Runtime', 'Pi', 'Goz')) {
        $target = Join-Path $store $relative
        New-Item -ItemType Directory -Force -Path (Join-Path $target 'deep\node_modules\package\nested') | Out-Null
        Set-Content -LiteralPath (Join-Path $target 'deep\node_modules\package\nested\keep.txt') -Value $relative -Encoding ASCII
        New-Item -ItemType Junction -Path (Join-Path $staging $relative) -Target $target | Out-Null
    }

    $runtimeLink = Join-Path $staging 'Runtime'
    Remove-Junction $runtimeLink
    if (Test-Path -LiteralPath $runtimeLink) { throw 'Runtime junction entry was not removed.' }
    if (-not (Test-Path -LiteralPath (Join-Path $store 'Runtime\deep\node_modules\package\nested\keep.txt') -PathType Leaf)) {
        throw 'Removing the Runtime junction modified its shared target.'
    }
    New-Item -ItemType Junction -Path $runtimeLink -Target (Join-Path $store 'Runtime') | Out-Null

    Set-Content -LiteralPath (Join-Path $staging 'MiaoDesk.exe') -Value 'stub' -Encoding ASCII
    Remove-DeploymentTree $staging
    if (Test-Path -LiteralPath $staging) { throw 'Deployment tree was not removed.' }

    foreach ($relative in @('Runtime', 'Pi', 'Goz')) {
        $sentinel = Join-Path $store "$relative\deep\node_modules\package\nested\keep.txt"
        if (-not (Test-Path -LiteralPath $sentinel -PathType Leaf)) {
            throw "Shared RuntimeBundle target was modified while cleaning deployment tree: $relative"
        }
    }

    Write-Host 'ARM64 updater junction cleanup regression test passed.' -ForegroundColor Green
}
finally {
    if (Test-Path -LiteralPath $staging) {
        foreach ($relative in @('Runtime', 'Pi', 'Goz')) {
            $child = Join-Path $staging $relative
            if (Test-ReparsePoint $child) { try { Remove-Junction $child } catch { } }
        }
        Remove-Item -LiteralPath $staging -Recurse -Force -Confirm:$false -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $store) { Remove-Item -LiteralPath $store -Recurse -Force -Confirm:$false -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force -Confirm:$false -ErrorAction SilentlyContinue }
}
