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
    # 这里列出的是**断言**,不是拷贝清单 —— 真正的拷贝是根 CMakeLists 的
    # install(DIRECTORY assets/wallpapers/ DESTINATION Wallpapers),整目录。
    # scripts/verify-staged-wallpaper-assets.sh 从每个包**真正生效的入口**
    # (manifest 的 legacy_entry 优先,见 WallpaperPackage.cpp)推出应有的集合与这里
    # 比对:少列一项,该文件是否真的进了打包产物就没有检查兜底。cloud.png 就是在
    # MiaoCloud scene.json 从空壳填成 5 层的同一天补上的 —— 前面四项一直在,
    # 第五项被漏掉了整整一天。
    # 2026-09-22:NeonCity 与 MysticMoon 的 10 个资产此前一条都没列。原因不是谁忘了
    # 写,而是那个门此前只读 scene.json,而这两个包的 scene.json 是空壳 —— 产品实际
    # 加载的是 scene.ini(legacy_entry 优先)。门推出 0 个资产,于是报"✅ 覆盖了每个
    # 资产"。门读错文件时,手写清单看着没问题也会一直漏。
    'Wallpapers\MiaoCloud.mdwall\assets\background.jpg',
    'Wallpapers\MiaoCloud.mdwall\assets\cat.png',
    'Wallpapers\MiaoCloud.mdwall\assets\cloud.png',
    'Wallpapers\MiaoCloud.mdwall\assets\tail.png',
    'Wallpapers\MiaoCloud.mdwall\assets\blink.png',
    'Wallpapers\NeonCity.mdwall\assets\background.jpg',
    'Wallpapers\NeonCity.mdwall\assets\city_glow.png',
    'Wallpapers\NeonCity.mdwall\assets\haze.png',
    'Wallpapers\NeonCity.mdwall\assets\rain_2.png',
    'Wallpapers\NeonCity.mdwall\assets\rain_1.png',
    'Wallpapers\MysticMoon.mdwall\assets\background.jpg',
    'Wallpapers\MysticMoon.mdwall\assets\moon_glow.png',
    'Wallpapers\MysticMoon.mdwall\assets\water_glow.png',
    'Wallpapers\MysticMoon.mdwall\assets\fog.png',
    'Wallpapers\MysticMoon.mdwall\assets\fireflies.png',
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
    # Get-Content -TotalCount 返回的是字符串数组,而 `$array -notmatch 're'` 在
    # PowerShell 里是「过滤」不是「布尔判断」—— 它返回不匹配的元素。前 6 行里只有
    # 一行是 name:,于是结果是非空数组、恒为真值,这里必然 throw。先 join 成单个
    # 字符串,并用 (?m) 让 ^/$ 按行锚定。
    $head = (Get-Content (Join-Path $Destination "skills\$skill\SKILL.md") -TotalCount 6) -join "`n"
    if ($head -notmatch ('(?m)^name:\s*' + [regex]::Escape($skill) + '\s*$')) {
        throw "SKILL.md frontmatter name does not match its directory: skills\$skill"
    }
}
Write-Host "Content skills staged and allowlist-consistent: $($expectedSkills -join ', ')" -ForegroundColor Cyan

# Every staged wallpaper image must be real image data, not a Git LFS pointer
# stub. These assets are LFS-tracked (see .gitattributes), so on a machine where
# `git lfs pull` never ran, a ~130-byte pointer would be staged and installed
# as-is. A pointer installs without error — it just paints nothing, which is the
# hardest kind of wallpaper bug to report.
#
# Enumerated from what is actually in the destination rather than a hand-written
# list. The previous version named 4 of the 15 wallpaper images, so a botched LFS
# pull in the other 11 was invisible; a list that has to be updated by hand every
# time a package gains a layer is a list that will not be.
$wallpaperImages = @(Get-ChildItem (Join-Path $Destination 'Wallpapers') -Recurse -File |
    Where-Object { $_.Extension -in '.png', '.jpg', '.jpeg', '.webp', '.gif' })
foreach ($image in $wallpaperImages) {
    if ($image.Length -lt 1024) {
        throw "Wallpaper image still looks like an LFS pointer: $($image.FullName)"
    }
}
Write-Host "Wallpaper images all real, not LFS pointers: $($wallpaperImages.Count)" -ForegroundColor Cyan

if (Test-Path (Join-Path $Destination 'node_modules')) {
    throw 'Install-root node_modules must not exist.'
}

Write-Host "Windows $Architecture package staging ready: $Destination" -ForegroundColor Green
