param(
    [string]$Source = (Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\app\MiaoMiao.png'),
    [string]$OutputDir = (Join-Path (Split-Path $PSScriptRoot -Parent) 'packaging\store\assets')
)

# Generates the Microsoft Partner Center listing assets from the canonical
# MiaoMiao.png. Uses the same crop geometry as scripts/generate-miaomiao-icon.ps1
# so the Store artwork matches the in-product icon exactly.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Drawing

$sourcePath = (Resolve-Path $Source).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Partner Center desktop-app submission sizes. StoreLogo-50 is shown beside the
# app name in the Store; AppIcon-256/512 are the primary app icons; the square
# sizes cover the remaining tile slots Partner Center may request.
$targets = @(
    @{ Name = 'Square44x44Logo.png'; Size = 44 },
    @{ Name = 'StoreLogo-50.png'; Size = 50 },
    @{ Name = 'Square71x71Logo.png'; Size = 71 },
    @{ Name = 'Square150x150Logo.png'; Size = 150 },
    @{ Name = 'AppIcon-256.png'; Size = 256 },
    @{ Name = 'Square300x300Logo.png'; Size = 300 },
    @{ Name = 'AppIcon-512.png'; Size = 512 }
)

function Get-CropScale([int]$Size) {
    if ($Size -le 24) { return 0.78 }
    if ($Size -le 48) { return 0.84 }
    if ($Size -le 96) { return 0.90 }
    return 0.96
}

$image = [System.Drawing.Image]::FromFile($sourcePath)
try {
    if ($image.Width -lt 512 -or $image.Height -lt 512) {
        throw "Source image must be at least 512x512, got $($image.Width)x$($image.Height)."
    }

    foreach ($target in $targets) {
        $size = $target.Size
        $scale = Get-CropScale $size
        $base = [Math]::Min($image.Width, $image.Height)
        $cropSize = [Math]::Max(1, [int][Math]::Round($base * $scale))

        $centerX = $image.Width * 0.50
        $centerY = $image.Height * 0.46
        $left = [int][Math]::Round($centerX - ($cropSize / 2.0))
        $top = [int][Math]::Round($centerY - ($cropSize / 2.0))
        $left = [Math]::Max(0, [Math]::Min($left, $image.Width - $cropSize))
        $top = [Math]::Max(0, [Math]::Min($top, $image.Height - $cropSize))

        $bitmap = [System.Drawing.Bitmap]::new(
            $size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode = [System.Drawing.PixelOffsetMode]::HighQuality
                $graphics.DrawImage(
                    $image,
                    [System.Drawing.Rectangle]::new(0, 0, $size, $size),
                    $left, $top, $cropSize, $cropSize,
                    [System.Drawing.GraphicsUnit]::Pixel)
            } finally {
                $graphics.Dispose()
            }

            $outPath = Join-Path $OutputDir $target.Name
            $bitmap.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
            $bytes = (Get-Item -LiteralPath $outPath).Length
            Write-Host ("{0,-26} {1,4}x{2,-4} {3,8:N0} bytes" -f $target.Name, $size, $size, $bytes)
        } finally {
            $bitmap.Dispose()
        }
    }
} finally {
    $image.Dispose()
}

Write-Host ""
Write-Host "Store assets written to: $OutputDir" -ForegroundColor Cyan
