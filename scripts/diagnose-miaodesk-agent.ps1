param(
    [int]$Tail = 120
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path $PSScriptRoot -Parent
$UserRoot = Join-Path $env:LOCALAPPDATA 'MiaoDesk'
$PreviewRoot = Join-Path $UserRoot 'DevPreview'
$RuntimeCache = Join-Path $UserRoot 'RuntimeCache'
$NativeTest = Join-Path $UserRoot 'NativeTest'
$SettingsPath = Join-Path $UserRoot 'model-settings.json'
$LogRoot = Join-Path ([Environment]::GetFolderPath('Desktop')) 'MiaoDesk-Logs'

function Section([string]$Text) {
    Write-Host "`n=== $Text ===" -ForegroundColor Cyan
}

function Safe-Endpoint([string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) { return '<empty>' }
    $cut = $Value.IndexOfAny([char[]]'?#')
    if ($cut -ge 0) { return $Value.Substring(0, $cut) + '?<redacted>' }
    return $Value
}

function Test-File([string]$Root, [string]$Relative) {
    $path = Join-Path $Root $Relative
    [pscustomobject]@{
        Root = $Root
        Relative = $Relative
        Exists = Test-Path $path -PathType Leaf
        Path = $path
    }
}

function Resolve-FirstRuntimeFile([string]$Relative) {
    foreach ($root in @($PreviewRoot, $RuntimeCache, $NativeTest)) {
        $path = Join-Path $root $Relative
        if (Test-Path $path -PathType Leaf) { return $path }
    }
    return $null
}

Section 'Checkout'
Set-Location $RepoRoot
$head = (& git rev-parse HEAD 2>$null).Trim()
Write-Host "Repo: $RepoRoot"
Write-Host "HEAD: $head"

Section 'Active model config (secrets omitted)'
if (Test-Path $SettingsPath -PathType Leaf) {
    try {
        $config = Get-Content $SettingsPath -Raw | ConvertFrom-Json
        Write-Host ("Provider: " + [string]$config.ProviderId)
        Write-Host ("Model:    " + [string]$config.Model)
        Write-Host ("BaseUrl:  " + (Safe-Endpoint ([string]$config.BaseUrl)))
        Write-Host ("Endpoint: " + (Safe-Endpoint ([string]$config.Endpoint)))
    }
    catch {
        Write-Host "model-settings.json parse failed: $($_.Exception.Message)" -ForegroundColor Yellow
    }
} else {
    Write-Host "Missing: $SettingsPath" -ForegroundColor Yellow
}

Section 'Runtime resolution'
$roots = @($PreviewRoot, $RuntimeCache, $NativeTest)
$checks = @(
    'Runtime\Node\node.exe',
    'Runtime\Node\node_modules\@deepseek-ai\dsh\lib\bin.js',
    'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js',
    'Goz\goz.exe',
    'Goz\gozd.exe'
)
foreach ($root in $roots) {
    Write-Host "[$root]"
    foreach ($relative in $checks) {
        $item = Test-File $root $relative
        $mark = if ($item.Exists) { 'OK ' } else { 'MISS' }
        Write-Host ("  {0}  {1}" -f $mark, $relative)
    }
}

$node = Resolve-FirstRuntimeFile 'Runtime\Node\node.exe'
$pi = Resolve-FirstRuntimeFile 'Pi\node_modules\@earendil-works\pi-coding-agent\dist\cli.js'
if ($node) {
    Write-Host "Resolved Node: $node" -ForegroundColor Green
    Section 'Bundled Node'
    & $node --version
}
if ($node -and $pi) {
    Write-Host "Resolved Pi:   $pi" -ForegroundColor Green
    Section 'Bundled Pi CLI'
    & $node $pi --version
}

Section 'Recent model credential guard log (secret contents never logged)'
$credentialLog = Join-Path $LogRoot 'model-credential.log'
if (Test-Path $credentialLog -PathType Leaf) {
    Get-Content $credentialLog -Tail $Tail
} else {
    Write-Host "No credential-guard log yet: $credentialLog" -ForegroundColor DarkYellow
    Write-Host 'The latest build writes only credential byte length, invalid character position/codepoint, and recovery status.' -ForegroundColor DarkYellow
}

Section 'Recent Pi runtime log'
$piLog = Join-Path $LogRoot 'pi-runtime.log'
if (Test-Path $piLog -PathType Leaf) {
    Get-Content $piLog -Tail $Tail
} else {
    Write-Host "No Pi log yet: $piLog" -ForegroundColor DarkYellow
}

Section 'Recent L3 route log'
$l3Log = Join-Path $LogRoot 'l3-runtime.log'
if (Test-Path $l3Log -PathType Leaf) {
    Get-Content $l3Log -Tail $Tail
} else {
    Write-Host "No L3 route log yet: $l3Log" -ForegroundColor DarkYellow
}

Section 'Recent staged WinHTTP log'
$httpLog = Join-Path $LogRoot 'l3-winhttp.log'
if (Test-Path $httpLog -PathType Leaf) {
    $lines = @(Get-Content $httpLog -Tail $Tail)
    $lines | ForEach-Object { Write-Host $_ }

    $lastFailure = $lines | Where-Object { $_ -match 'FAILED error=' } | Select-Object -Last 1
    if ($lastFailure) {
        Write-Host "`nLast transport failure:" -ForegroundColor Yellow
        Write-Host $lastFailure -ForegroundColor Yellow
        if ($lastFailure -match 'WinHttpOpenRequest') {
            Write-Host 'Hint: request path/verb/URL normalization is the first suspect.' -ForegroundColor Yellow
        } elseif ($lastFailure -match 'WinHttpSendRequest') {
            Write-Host 'Hint: headers/body/WinHTTP send parameters are the first suspect. Check model-credential.log first if error=87.' -ForegroundColor Yellow
        } elseif ($lastFailure -match 'WinHttpReceiveResponse') {
            Write-Host 'Hint: connection/TLS/proxy/server response is the first suspect.' -ForegroundColor Yellow
        } elseif ($lastFailure -match 'WinHttpConnect') {
            Write-Host 'Hint: host/port/proxy resolution is the first suspect.' -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "No staged WinHTTP log yet: $httpLog" -ForegroundColor DarkYellow
    Write-Host 'Run one chat request with the latest Fast Preview, then run this diagnostic again.' -ForegroundColor DarkYellow
}

Section 'Where to send the result'
Write-Host 'Paste the credential-guard section, Pi runtime tail, and final staged WinHTTP failure line back into the chat.'
Write-Host 'API key contents are never read or printed by this diagnostic.' -ForegroundColor Green