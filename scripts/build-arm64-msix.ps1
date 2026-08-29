param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [string]$OutputPath = (Join-Path $PWD 'artifacts\miaomiao-arm64.msix'),
    [string]$Version = '0.1.0.0',
    [string]$Publisher = 'CN=GoodLoongStudio',
    [string]$PackageName = 'GoodLoongStudio.MiaoMiao'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$builder = Join-Path $PSScriptRoot 'build-msix.ps1'
& $builder `
    -InputDirectory $InputDirectory `
    -Architecture arm64 `
    -OutputPath $OutputPath `
    -Version $Version `
    -Publisher $Publisher `
    -PackageName $PackageName
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
