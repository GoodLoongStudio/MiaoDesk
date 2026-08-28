$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$paths = @{
    Header = Join-Path $root 'src/native/include/turingdesk/StoreDemoExperience.h'
    Demo = Join-Path $root 'src/native/src/desktop/demo/StoreDemoExperience.cpp'
    AiPage = Join-Path $root 'src/native/src/ui/settings/DesktopAiSettingsPage.cpp'
    Conversation = Join-Path $root 'src/native/src/ui/ai/ConversationPanelImpl.inc'
    Search = Join-Path $root 'src/native/src/ui/search/SearchWindow.cpp'
    Library = Join-Path $root 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2.cpp'
    CMake = Join-Path $root 'src/native/CMakeLists.txt'
    Plan = Join-Path $root 'docs/STORE_DEMO_V0.1_PLAN.md'
    Acceptance = Join-Path $root 'docs/STORE_DEMO_V0.1_ACCEPTANCE.md'
}

foreach ($entry in $paths.GetEnumerator()) {
    if (-not (Test-Path $entry.Value)) { throw "Store Demo file missing: $($entry.Key) -> $($entry.Value)" }
}

$header = Get-Content $paths.Header -Raw
$demo = Get-Content $paths.Demo -Raw
$ai = Get-Content $paths.AiPage -Raw
$conversation = Get-Content $paths.Conversation -Raw
$search = Get-Content $paths.Search -Raw
$library = Get-Content $paths.Library -Raw
$cmake = Get-Content $paths.CMake -Raw

foreach ($marker in @(
    'IsStoreDemoScopeEnabled', 'HideAdvancedWorkbench', 'NeedsFirstRun', 'MarkFirstRunCompleted',
    'ApplyShowcaseWallpaper', 'EnsureShowcaseClocks', 'RunGoldenPath', 'TryHandleDemoPrompt',
    'MaybeShowFirstRun', 'OfferGoldenPath')) {
    if (-not $header.Contains($marker)) { throw "StoreDemoExperience.h missing API: $marker" }
}

foreach ($marker in @(
    'store-demo.ini', 'scene-aurora', '极简时钟', '日期时钟', '玻璃时钟',
    '一键体验', '演示模式', 'WidgetFixedPreset::GlassClock')) {
    if (-not $demo.Contains($marker)) { throw "StoreDemoExperience.cpp missing marker: $marker" }
}

foreach ($marker in @(
    'StoreDemoExperience.h', 'HideAdvancedWorkbench', '立即体验动态桌面', 'kDemoGoldenId',
    'RunDemoGoldenPath')) {
    if (-not $ai.Contains($marker)) { throw "DesktopAiSettingsPage missing Store Demo marker: $marker" }
}
if ($ai.Contains('打开秒喵工作台') -and -not $ai.Contains('HideAdvancedWorkbench')) {
    throw 'Harness entry must remain gated behind HideAdvancedWorkbench for Store Demo.'
}

foreach ($marker in @(
    'TryHandleDemoPrompt', '演示模式', '一键体验', 'StoreDemoExperience.h')) {
    if (-not $conversation.Contains($marker)) { throw "ConversationPanel missing demo marker: $marker" }
}

foreach ($marker in @('MaybeShowFirstRun', 'kFirstRunTimerId', 'StoreDemoExperience.h')) {
    if (-not $search.Contains($marker)) { throw "SearchWindow missing first-run marker: $marker" }
}

foreach ($marker in @('一键体验套装', 'kWidgetDemoId', 'OfferGoldenPath')) {
    if (-not $library.Contains($marker)) { throw "WallpaperLibraryWindowV2 missing showcase marker: $marker" }
}

if (-not $cmake.Contains('src/desktop/demo/StoreDemoExperience.cpp')) {
    throw 'CMakeLists.txt must compile StoreDemoExperience.cpp'
}
if (-not $cmake.Contains('src/desktop/widgets/DesktopWidgetController.cpp')) {
    throw 'App target must include DesktopWidgetController for Store Demo clocks.'
}

Write-Host 'Store Demo scope OK: first-run, no-Key golden path, hidden advanced workbench, and showcase entry points remain wired.'
