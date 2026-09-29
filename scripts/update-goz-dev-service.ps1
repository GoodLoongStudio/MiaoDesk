param(
  [Parameter(Mandatory=$true)][string]$SourceGoz,
  [Parameter(Mandatory=$true)][string]$DestinationGoz
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  throw 'This helper must run elevated.'
}

if (-not (Test-Path (Join-Path $SourceGoz 'goz.exe') -PathType Leaf) -or
    -not (Test-Path (Join-Path $SourceGoz 'gozd.exe') -PathType Leaf)) {
  throw "Source Goz runtime is incomplete: $SourceGoz"
}

$oldGoZd = Join-Path $DestinationGoz 'gozd.exe'
if (Test-Path $oldGoZd -PathType Leaf) {
  & $oldGoZd uninstall | Out-Host
  Start-Sleep -Milliseconds 500
}

$svc = Get-Service -Name 'goz' -ErrorAction SilentlyContinue
if ($svc) {
  sc.exe stop goz | Out-Host
  Start-Sleep -Milliseconds 500
  sc.exe delete goz | Out-Host
  Start-Sleep -Milliseconds 500
}

Remove-Item $DestinationGoz -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $DestinationGoz | Out-Null
Copy-Item (Join-Path $SourceGoz '*') $DestinationGoz -Recurse -Force

$newGoZd = Join-Path $DestinationGoz 'gozd.exe'
& $newGoZd install | Out-Host
if ($LASTEXITCODE -ne 0) { throw "gozd install failed: $LASTEXITCODE" }

$goz = Join-Path $DestinationGoz 'goz.exe'
$ready = $false
for ($i=0; $i -lt 30; $i++) {
  & $goz --status *> $null
  if ($LASTEXITCODE -eq 0) { $ready = $true; break }
  Start-Sleep -Seconds 1
}
if (-not $ready) { throw 'goz service did not become queryable.' }

Write-Host 'Goz dev service ready.' -ForegroundColor Green
