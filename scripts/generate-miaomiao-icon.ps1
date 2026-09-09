param(
    [string]$Source = (Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\app\MiaoMiao.png'),
    [string]$Destination = (Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\app\MiaoMiao.ico')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Drawing

$sourcePath = (Resolve-Path $Source).Path
$destinationPath = [System.IO.Path]::GetFullPath($Destination)
$destinationDir = Split-Path $destinationPath -Parent
New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null

# One canonical PNG feeds every Windows icon surface. Small frames are cropped
# progressively tighter so the cat face remains readable in the notification
# area and taskbar instead of shrinking into a large field of background.
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$image = [System.Drawing.Image]::FromFile($sourcePath)
try {
    function Get-CropScale([int]$Size) {
        if ($Size -le 24) { return 0.78 }
        if ($Size -le 48) { return 0.84 }
        if ($Size -le 96) { return 0.90 }
        return 0.96
    }

    function New-IconFrame([System.Drawing.Image]$SourceImage, [int]$Size) {
        $scale = Get-CropScale $Size
        $base = [Math]::Min($SourceImage.Width, $SourceImage.Height)
        $cropSize = [Math]::Max(1, [int][Math]::Round($base * $scale))

        $centerX = $SourceImage.Width * 0.50
        # MiaoMiao.png intentionally has more breathing room below the face.
        # Bias the crop upward slightly so tiny Windows icons are face-first.
        $centerY = $SourceImage.Height * 0.46
        $left = [int][Math]::Round($centerX - ($cropSize / 2.0))
        $top = [int][Math]::Round($centerY - ($cropSize / 2.0))
        $left = [Math]::Max(0, [Math]::Min($left, $SourceImage.Width - $cropSize))
        $top = [Math]::Max(0, [Math]::Min($top, $SourceImage.Height - $cropSize))

        $bitmap = [System.Drawing.Bitmap]::new(
            $Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
                $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
                $graphics.DrawImage(
                    $SourceImage,
                    [System.Drawing.Rectangle]::new(0, 0, $Size, $Size),
                    $left, $top, $cropSize, $cropSize,
                    [System.Drawing.GraphicsUnit]::Pixel)
            } finally {
                $graphics.Dispose()
            }

            $stream = [System.IO.MemoryStream]::new()
            try {
                $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
                return $stream.ToArray()
            } finally {
                $stream.Dispose()
            }
        } finally {
            $bitmap.Dispose()
        }
    }

    $frames = foreach ($size in $sizes) {
        $data = New-IconFrame $image $size
        [pscustomobject]@{ Size = $size; Data = $data }
    }

    $file = [System.IO.File]::Open(
        $destinationPath,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None)
    try {
        $writer = [System.IO.BinaryWriter]::new($file)
        try {
            # ICONDIR
            $writer.Write([UInt16]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]$frames.Count)

            $offset = 6 + (16 * $frames.Count)
            foreach ($frame in $frames) {
                $dimension = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
                $writer.Write([byte]$dimension)
                $writer.Write([byte]$dimension)
                $writer.Write([byte]0)   # color count
                $writer.Write([byte]0)   # reserved
                $writer.Write([UInt16]1) # planes
                $writer.Write([UInt16]32)
                $writer.Write([UInt32]$frame.Data.Length)
                $writer.Write([UInt32]$offset)
                $offset += $frame.Data.Length
            }

            foreach ($frame in $frames) {
                $writer.Write([byte[]]$frame.Data)
            }
        } finally {
            $writer.Dispose()
        }
    } finally {
        $file.Dispose()
    }
} finally {
    $image.Dispose()
}

# Structural validation: all expected sizes must exist and every directory entry
# must point to a complete PNG frame. This catches the truncated-ICO regression
# that previously let Explorer fall back to an older cached icon.
$bytes = [System.IO.File]::ReadAllBytes($destinationPath)
$readerStream = [System.IO.MemoryStream]::new($bytes, $false)
$reader = [System.IO.BinaryReader]::new($readerStream)
try {
    if ($reader.ReadUInt16() -ne 0 -or $reader.ReadUInt16() -ne 1) { throw 'Generated file is not an ICO.' }
    $count = $reader.ReadUInt16()
    if ($count -ne $sizes.Count) { throw "Generated ICO frame count mismatch: $count" }

    $found = New-Object System.Collections.Generic.List[int]
    for ($i = 0; $i -lt $count; $i++) {
        $widthByte = $reader.ReadByte()
        $heightByte = $reader.ReadByte()
        [void]$reader.ReadByte()
        [void]$reader.ReadByte()
        [void]$reader.ReadUInt16()
        [void]$reader.ReadUInt16()
        $length = $reader.ReadUInt32()
        $offset = $reader.ReadUInt32()
        $width = if ($widthByte -eq 0) { 256 } else { [int]$widthByte }
        $height = if ($heightByte -eq 0) { 256 } else { [int]$heightByte }
        if ($width -ne $height) { throw "Non-square ICO frame: ${width}x${height}" }
        if (($offset + $length) -gt $bytes.Length) { throw "Truncated ICO frame: ${width}x${height}" }
        if ($length -lt 8) { throw "ICO frame too small: ${width}x${height}" }
        $pngSignature = @(137,80,78,71,13,10,26,10)
        for ($j = 0; $j -lt 8; $j++) {
            if ($bytes[$offset + $j] -ne $pngSignature[$j]) { throw "ICO frame is not PNG: ${width}x${height}" }
        }
        $found.Add($width)
    }

    foreach ($size in $sizes) {
        if (-not $found.Contains($size)) { throw "Generated ICO is missing ${size}x${size}." }
    }
} finally {
    $reader.Dispose()
    $readerStream.Dispose()
}

$hash = (Get-FileHash -Algorithm SHA256 $destinationPath).Hash.ToLowerInvariant()
Write-Host "MiaoMiao icon generated from $sourcePath" -ForegroundColor Green
Write-Host "Frames: $($sizes -join ', ') px" -ForegroundColor Cyan
Write-Host "ICO: $destinationPath bytes=$($bytes.Length) sha256=$hash" -ForegroundColor Cyan
