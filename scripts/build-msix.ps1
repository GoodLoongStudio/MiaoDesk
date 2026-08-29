param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [Parameter(Mandatory = $true)][ValidateSet('arm64', 'x64')][string]$Architecture,
    [string]$OutputPath = (Join-Path $PWD 'artifacts\miaomiao.msix'),
    [string]$Version = '0.1.0.0',
    [string]$Publisher = 'CN=GoodLoongStudio',
    [string]$PackageName = 'GoodLoongStudio.MiaoMiao'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$input = (Resolve-Path $InputDirectory).Path
foreach ($required in @('TuringDesk.exe', 'TuringDeskWallpaper.exe', 'TuringDeskHarness.exe')) {
    if (-not (Test-Path (Join-Path $input $required) -PathType Leaf)) {
        throw "InputDirectory is missing $required."
    }
}
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw 'Version must be four numeric parts, e.g. 0.1.0.0.'
}

$repo = Split-Path $PSScriptRoot -Parent
$template = Join-Path $repo 'packaging\windows-store\AppxManifest.xml.in'
$brandRoot = Join-Path $repo 'packaging\windows-store\assets'
$appIconSource = Join-Path $brandRoot 'AppIcon64.png'
$trayIconSource = Join-Path $brandRoot 'TrayIcon64.png'
foreach ($asset in @($template, $appIconSource, $trayIconSource)) {
    if (-not (Test-Path $asset -PathType Leaf)) {
        throw "Missing Store packaging asset: $asset"
    }
}

$stage = Join-Path $env:TEMP ("miaomiao-msix-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
    Copy-Item (Join-Path $input '*') $stage -Recurse -Force

    $manifest = Get-Content $template -Raw -Encoding UTF8
    $manifest = $manifest.Replace('__PACKAGE_NAME__', $PackageName)
    $manifest = $manifest.Replace('__PUBLISHER__', $Publisher)
    $manifest = $manifest.Replace('__VERSION__', $Version)
    $manifest = $manifest.Replace('__ARCH__', $Architecture)
    Set-Content -Path (Join-Path $stage 'AppxManifest.xml') -Value $manifest -Encoding UTF8

    $assets = Join-Path $stage 'Assets'
    New-Item -ItemType Directory -Force -Path $assets | Out-Null
    Add-Type -AssemblyName System.Drawing

    function Write-StoreImage([string]$Source, [string]$Target, [int]$Width, [int]$Height) {
        $src = [System.Drawing.Image]::FromFile($Source)
        try {
            $bitmap = New-Object System.Drawing.Bitmap $Width, $Height
            try {
                $g = [System.Drawing.Graphics]::FromImage($bitmap)
                try {
                    $g.Clear([System.Drawing.Color]::FromArgb(255, 26, 119, 237))
                    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                    $side = [Math]::Min($Width, $Height)
                    $x = [int](($Width - $side) / 2)
                    $y = [int](($Height - $side) / 2)
                    $g.DrawImage($src, $x, $y, $side, $side)
                } finally {
                    $g.Dispose()
                }
                $bitmap.Save($Target, [System.Drawing.Imaging.ImageFormat]::Png)
            } finally {
                $bitmap.Dispose()
            }
        } finally {
            $src.Dispose()
        }
    }

    Write-StoreImage $appIconSource (Join-Path $assets 'StoreLogo.png') 50 50
    Write-StoreImage $appIconSource (Join-Path $assets 'Square44x44Logo.png') 44 44
    Write-StoreImage $appIconSource (Join-Path $assets 'Square150x150Logo.png') 150 150
    Write-StoreImage $appIconSource (Join-Path $assets 'Wide310x150Logo.png') 310 150

    function Write-PngIcon([string]$Source, [string]$Target) {
        [byte[]]$png = [System.IO.File]::ReadAllBytes($Source)
        $stream = [System.IO.File]::Open($Target, [System.IO.FileMode]::Create)
        try {
            $writer = New-Object System.IO.BinaryWriter $stream
            try {
                $writer.Write([uint16]0)
                $writer.Write([uint16]1)
                $writer.Write([uint16]1)
                $writer.Write([byte]64)
                $writer.Write([byte]64)
                $writer.Write([byte]0)
                $writer.Write([byte]0)
                $writer.Write([uint16]1)
                $writer.Write([uint16]32)
                $writer.Write([uint32]$png.Length)
                $writer.Write([uint32]22)
                $writer.Write($png)
            } finally {
                $writer.Dispose()
            }
        } finally {
            $stream.Dispose()
        }
    }

    Write-PngIcon $trayIconSource (Join-Path $assets 'MiaoMiao.ico')

    $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $hostArchitecture = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
    $makeAppx = Get-ChildItem $sdkBin -Recurse -Filter MakeAppx.exe -File |
        Where-Object { (Split-Path $_.DirectoryName -Leaf) -ieq $hostArchitecture } |
        Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $makeAppx) {
        $makeAppx = Get-ChildItem $sdkBin -Recurse -Filter MakeAppx.exe -File |
            Sort-Object FullName -Descending | Select-Object -First 1
    }
    if (-not $makeAppx) {
        throw 'MakeAppx.exe was not found in the Windows SDK.'
    }

    $outputDir = Split-Path $OutputPath -Parent
    if ($outputDir) {
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    }
    & $makeAppx.FullName pack /d $stage /p $OutputPath /o
    if ($LASTEXITCODE -ne 0) {
        throw "MakeAppx failed with exit code $LASTEXITCODE."
    }
    Write-Host "MSIX created: $OutputPath"
} finally {
    Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
}
