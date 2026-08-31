param(
    [Parameter(Mandatory = $true)][Alias('PackagePath')][string]$BundlePath,
    [Parameter(Mandatory = $true)][string]$CertificatePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$packagePath = (Resolve-Path $BundlePath).Path
$certificate = (Resolve-Path $CertificatePath).Path

Write-Host 'Installing MiaoDesk test signing certificate into CurrentUser\TrustedPeople...'
Import-Certificate -FilePath $certificate -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' | Out-Null

Write-Host ("Installing MiaoDesk test package: {0}" -f (Split-Path $packagePath -Leaf))
Add-AppxPackage -Path $packagePath -ForceApplicationShutdown

$package = Get-AppxPackage -Name 'GoodLoongStudio.MiaoMiao' | Sort-Object Version -Descending | Select-Object -First 1
if (-not $package) { throw 'MiaoDesk test package was not found after Add-AppxPackage.' }

Write-Host ("Installed: {0} {1} ({2})" -f $package.Name, $package.Version, $package.Architecture) -ForegroundColor Green
Write-Host 'Launch MiaoDesk from the Start menu.' -ForegroundColor Green
