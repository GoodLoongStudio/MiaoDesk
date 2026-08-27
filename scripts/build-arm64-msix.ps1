param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [string]$OutputPath = (Join-Path $PWD 'artifacts\miaomiao-arm64.msix'),
    [string]$Version = '0.1.0.0',
    [string]$Publisher = 'CN=GoodLoongStudio',
    [string]$PackageName = 'GoodLoongStudio.MiaoMiao'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$input = (Resolve-Path $InputDirectory).Path
foreach ($required in @('TuringDesk.exe','TuringDeskWallpaper.exe','TuringDeskHarness.exe')) { if (-not (Test-Path (Join-Path $input $required) -PathType Leaf)) { throw "InputDirectory is missing $required." } }
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') { throw 'Version must be four numeric parts, e.g. 0.1.0.0.' }
$repo = Split-Path $PSScriptRoot -Parent
$template = Join-Path $repo 'packaging\windows-store\AppxManifest.xml.in'
$stage = Join-Path $env:TEMP ("miaomiao-msix-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
  Copy-Item (Join-Path $input '*') $stage -Recurse -Force
  $manifest = Get-Content $template -Raw -Encoding UTF8
  $manifest = $manifest.Replace('__PACKAGE_NAME__',$PackageName).Replace('__PUBLISHER__',$Publisher).Replace('__VERSION__',$Version)
  Set-Content -Path (Join-Path $stage 'AppxManifest.xml') -Value $manifest -Encoding UTF8
  $assets = Join-Path $stage 'Assets'; New-Item -ItemType Directory -Force -Path $assets | Out-Null
  Add-Type -AssemblyName System.Drawing
  function New-MiaoMiaoLogo([string]$Path,[int]$Width,[int]$Height) {
    $bitmap = New-Object System.Drawing.Bitmap $Width,$Height
    try { $g=[System.Drawing.Graphics]::FromImage($bitmap); try { $g.Clear([System.Drawing.Color]::FromArgb(255,245,246,248)); $b=New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255,35,38,45)); try { $fs=[Math]::Max(12,[Math]::Min($Width,$Height)*0.36); $f=New-Object System.Drawing.Font 'Segoe UI',$fs,([System.Drawing.FontStyle]::Bold),([System.Drawing.GraphicsUnit]::Pixel); try { $fmt=New-Object System.Drawing.StringFormat; $fmt.Alignment=[System.Drawing.StringAlignment]::Center; $fmt.LineAlignment=[System.Drawing.StringAlignment]::Center; $g.DrawString('M',$f,$b,(New-Object System.Drawing.RectangleF 0,0,$Width,$Height),$fmt) } finally { $f.Dispose() } } finally { $b.Dispose() } } finally { $g.Dispose() }; $bitmap.Save($Path,[System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
  }
  New-MiaoMiaoLogo (Join-Path $assets 'StoreLogo.png') 50 50
  New-MiaoMiaoLogo (Join-Path $assets 'Square44x44Logo.png') 44 44
  New-MiaoMiaoLogo (Join-Path $assets 'Square150x150Logo.png') 150 150
  New-MiaoMiaoLogo (Join-Path $assets 'Wide310x150Logo.png') 310 150
  $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
  $hostArchitecture = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
  $makeAppx = Get-ChildItem $sdkBin -Recurse -Filter MakeAppx.exe -File |
    Where-Object { (Split-Path $_.DirectoryName -Leaf) -ieq $hostArchitecture } |
    Sort-Object FullName -Descending | Select-Object -First 1
  if (-not $makeAppx) { $makeAppx = Get-ChildItem $sdkBin -Recurse -Filter MakeAppx.exe -File | Sort-Object FullName -Descending | Select-Object -First 1 }
  if (-not $makeAppx) { throw 'MakeAppx.exe was not found in the Windows SDK.' }
  $outputDir=Split-Path $OutputPath -Parent; if ($outputDir) { New-Item -ItemType Directory -Force -Path $outputDir | Out-Null }
  & $makeAppx.FullName pack /d $stage /p $OutputPath /o
  if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed with exit code $LASTEXITCODE." }
  Write-Host "MSIX created: $OutputPath"
} finally { Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue }
