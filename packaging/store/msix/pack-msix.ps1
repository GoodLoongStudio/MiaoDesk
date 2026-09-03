param(
    [Parameter(Mandatory = $true)][string]$StageRoot,
    [Parameter(Mandatory = $false)][string]$ConfigPath = (Join-Path $PSScriptRoot 'store-config.json'),
    [Parameter(Mandatory = $false)][string]$ManifestTemplate = (Join-Path $PSScriptRoot 'AppxManifest.template.xml'),
    [Parameter(Mandatory = $false)][string]$StoreAssetsDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'assets'),
    [Parameter(Mandatory = $false)][string]$OutputMsix,
    [Parameter(Mandatory = $false)][string]$OutputMsixUpload,
    [Parameter(Mandatory = $false)][string]$WorkRoot = $env:TEMP
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path $StageRoot -PathType Container)) { throw "StageRoot not found: $StageRoot" }
if (-not (Test-Path $ConfigPath -PathType Leaf)) { throw "Config not found: $ConfigPath" }

$cfg = Get-Content $ConfigPath -Raw | ConvertFrom-Json

# Guard against unedited placeholders.
foreach ($key in @('identityName', 'publisher')) {
    if ($cfg.$key -like 'REPLACE_ME*') { throw "store-config.json '$key' is still a placeholder." }
}

if (-not $OutputMsix) { $OutputMsix = Join-Path $StageRoot (($cfg.packageName) + '-x64.msix') }
if (-not $OutputMsixUpload) { $OutputMsixUpload = [IO.Path]::ChangeExtension($OutputMsix, '.msixupload') }

# 1. Locate MakeAppx.exe from the Windows SDK.
$makeAppx = $null
$kitRoot = 'C:\Program Files (x86)\Windows Kits\10\bin'
if (Test-Path $kitRoot) {
    $makeAppx = Get-ChildItem $kitRoot -Recurse -Filter MakeAppx.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'x64\\MakeAppx\.exe$' } |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $makeAppx) {
    $cmd = Get-Command MakeAppx.exe -ErrorAction SilentlyContinue
    if ($cmd) { $makeAppx = $cmd.Source }
}
if (-not $makeAppx) { throw 'MakeAppx.exe was not found. Install the Windows SDK (MakeAppx/desktop tools).' }

# 2. Build the package content tree: staged product + store logos + rendered manifest.
$content = Join-Path $WorkRoot ("msix-content-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $content | Out-Null
try {
    robocopy $StageRoot $content /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -gt 7) { throw "robocopy failed with code $LASTEXITCODE" }

    $assetsDir = Join-Path $content 'Assets'
    New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null
    Get-ChildItem $StoreAssetsDir -File | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $assetsDir $_.Name) -Force
    }

    $replacements = @{
        '{{IDENTITY_NAME}}'           = $cfg.identityName
        '{{PUBLISHER}}'               = $cfg.publisher
        '{{PUBLISHER_DISPLAY_NAME}}'  = $cfg.publisherDisplayName
        '{{PACKAGE_NAME}}'            = $cfg.packageName
        '{{DESCRIPTION}}'             = $cfg.description
        '{{VERSION}}'                 = $cfg.version
        '{{TARGET_DEVICE_FAMILY}}'    = $cfg.targetDeviceFamily
        '{{MIN_VERSION}}'             = $cfg.minVersion
    }
    $manifestText = Get-Content $ManifestTemplate -Raw
    foreach ($k in $replacements.Keys) { $manifestText = $manifestText.Replace($k, $replacements[$k]) }
    Set-Content -Path (Join-Path $content 'AppxManifest.xml') -Value $manifestText -Encoding UTF8

    # 3. Pack unsigned .msix. Microsoft Store signs the package with its own cert.
    Write-Host "MakeAppx: $makeAppx"
    & $makeAppx pack /d $content /p $OutputMsix /o
    if ($LASTEXITCODE -ne 0) { throw "MakeAppx pack failed with exit code $LASTEXITCODE" }
    if (-not (Test-Path $OutputMsix -PathType Leaf)) { throw 'MSIX output file was not created.' }

    # 4. .msixupload is a ZIP wrapper containing the .msix at its root.
    Remove-Item $OutputMsixUpload -Force -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::Open($OutputMsixUpload, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $OutputMsix, [IO.Path]::GetFileName($OutputMsix), [System.IO.Compression.CompressionLevel]::NoCompression) | Out-Null
    } finally {
        $zip.Dispose()
    }

    Write-Host "MSIX: $OutputMsix ($((Get-Item $OutputMsix).Length) bytes)" -ForegroundColor Cyan
    Write-Host "MSIXUPLOAD: $OutputMsixUpload ($((Get-Item $OutputMsixUpload).Length) bytes)" -ForegroundColor Cyan
} finally {
    Remove-Item $content -Recurse -Force -ErrorAction SilentlyContinue
}
