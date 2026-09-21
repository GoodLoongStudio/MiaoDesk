param(
    [Parameter(Mandatory = $true)][string]$Destination,
    [Parameter(Mandatory = $true)][ValidateSet('x64','arm64')][string]$Architecture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-File([string]$Relative) {
    $path = Join-Path $Destination $Relative
    if (-not (Test-Path $path -PathType Leaf)) { throw "Package staging is missing: $Relative" }
}

foreach ($relative in @(
    'MiaoDesk.exe',
    'MiaoDeskWallpaper.exe',
    'MiaoDeskHarness.exe',
    'Assets\MiaoMiao.ico',
    'Wallpapers\MiaoCloud.mdwall\manifest.json',
    'Wallpapers\MiaoCloud.mdwall\scene.json',
    'Wallpapers\MiaoCloud.mdwall\scene.ini',
    'Wallpapers\NeonCity.mdwall\manifest.json',
    'Wallpapers\NeonCity.mdwall\scene.json',
    'Wallpapers\NeonCity.mdwall\scene.ini',
    'Wallpapers\MysticMoon.mdwall\manifest.json',
    'Wallpapers\MysticMoon.mdwall\scene.json',
    'Wallpapers\MysticMoon.mdwall\scene.ini'
)) { Assert-File $relative }

& (Join-Path $PSScriptRoot 'prepare-runtime-base.ps1') -DeployDir $Destination -Architecture $Architecture
if ($LASTEXITCODE -ne 0) { throw "Base Runtime staging failed with exit code $LASTEXITCODE" }

& (Join-Path $PSScriptRoot 'build-agent-runtime.ps1') -Root $Destination -Architecture $Architecture
if ($LASTEXITCODE -ne 0) { throw "Agent Runtime staging failed with exit code $LASTEXITCODE" }

foreach ($relative in @(
    'Runtime\Node\node.exe',
    'AI\package.json',
    'AI\package-lock.json',
    'AI\node_modules\@deepseek-ai\dsh\lib\bin.js',
    'AI\node_modules\pi\dist\cli.js',
    'Goz\goz.exe',
    'Goz\gozd.exe',
    'Wallpapers\MiaoCloud.mdwall\assets\background.jpg',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\MiaoCloud.mdwall\assets\tail.png',
    'Wallpapers\MiaoCloud.mdwall\assets\blink.png',
    'Wallpapers\NeonCity.mdwall\manifest.json',
    'Wallpapers\MysticMoon.mdwall\manifest.json',
    'skills\README.md',
    'skills\content-package-basics\SKILL.md',
    'skills\wallpaper-content\SKILL.md',
    'skills\widget-content\SKILL.md',
    'skills\content-review\SKILL.md'
)) { Assert-File $relative }

# The staged skill set must match the closed allowlist in
# src/ai/tools/NativeTools.cpp (kContentSkills). A skill that exists in the repo but
# is neither staged nor allowlisted is unreachable at runtime; one that is staged but
# not allowlisted is dead weight. Both directions fail the build.
$expectedSkills = @('content-package-basics', 'wallpaper-content', 'widget-content', 'content-review')
$stagedSkills = @(Get-ChildItem (Join-Path $Destination 'skills') -Directory | Select-Object -ExpandProperty Name)
$missing = @($expectedSkills | Where-Object { $stagedSkills -notcontains $_ })
if ($missing.Count -gt 0) { throw "Staged skills are missing: $($missing -join ', ')" }
$unexpected = @($stagedSkills | Where-Object { $expectedSkills -notcontains $_ })
if ($unexpected.Count -gt 0) { throw "Staged skill is absent from the kContentSkills allowlist: $($unexpected -join ', ')" }
foreach ($skill in $expectedSkills) {
    $head = Get-Content (Join-Path $Destination "skills\$skill\SKILL.md") -TotalCount 6
    if ($head -notmatch ('^name:\s*' + [regex]::Escape($skill) + '\s*$')) {
        throw "SKILL.md frontmatter name does not match its directory: skills\$skill"
    }
}
Write-Host "Content skills staged and allowlist-consistent: $($expectedSkills -join ', ')" -ForegroundColor Cyan

foreach ($relative in @(
    'Wallpapers\MiaoCloud.mdwall\assets\background.jpg',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\MiaoCloud.mdwall\assets\tail.png',
    'Wallpapers\MiaoCloud.mdwall\assets\blink.png'
)) {
    if ((Get-Item (Join-Path $Destination $relative)).Length -lt 1024) {
        throw "Wallpaper image still looks like an LFS pointer: $relative"
    }
}

if (Test-Path (Join-Path $Destination 'node_modules')) {
    throw 'Install-root node_modules must not exist.'
}

Write-Host "Windows $Architecture package staging ready: $Destination" -ForegroundColor Green
