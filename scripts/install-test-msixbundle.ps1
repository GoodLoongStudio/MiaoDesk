param(
    [Parameter(Mandatory = $true)][string]$BundlePath,
    [Parameter(Mandatory = $true)][string]$CertificatePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$bundle = (Resolve-Path $BundlePath).Path
$certificate = (Resolve-Path $CertificatePath).Path

Write-Host 'Installing MiaoDesk test signing certificate into CurrentUser\TrustedPeople...'
Import-Certificate -FilePath $certificate -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' | Out-Null

Write-Host 'Installing MiaoDesk x64/ARM64 test bundle...'
Add-AppxPackage -Path $bundle -ForceApplicationShutdown

$package = Get-AppxPackage -Name 'GoodLoongStudio.MiaoMiao' | Sort-Object Version -Descending | Select-Object -First 1
if (-not $package) { throw 'MiaoDesk test package was not found after Add-AppxPackage.' }

Write-Host ("Installed: {0} {1} ({2})" -f $package.Name, $package.Version, $package.Architecture) -ForegroundColor Green
Write-Host 'Launch 妙喵 from the Start menu.' -ForegroundColor Green
