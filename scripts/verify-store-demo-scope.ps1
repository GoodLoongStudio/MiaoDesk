$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function From-B64([string]$Value) {
    return [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Value))
}

$paths = @{
    Header = Join-Path $root 'src/native/include/miaodesk/StoreDemoExperience.h'
    Demo = Join-Path $root 'src/native/src/desktop/demo/StoreDemoExperience.cpp'
    AiPage = Join-Path $root 'src/native/src/ui/settings/DesktopAiSettingsPage.cpp'
    HarnessSettings = Join-Path $root 'src/native/src/harness/HarnessSettingsBridge.cpp'
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
$harnessSettings = Get-Content $paths.HarnessSettings -Raw
$conversation = Get-Content $paths.Conversation -Raw
$search = Get-Content $paths.Search -Raw
$library = Get-Content $paths.Library -Raw
$cmake = Get-Content $paths.CMake -Raw

foreach ($marker in @(
    'IsStoreDemoScopeEnabled', 'HideAdvancedWorkbench', 'NeedsFirstRun', 'MarkFirstRunCompleted',
    'ApplyShowcaseWallpaper', 'EnsureShowcaseWidgets', 'RunGoldenPath', 'TryHandleDemoPrompt',
    'MaybeShowFirstRun', 'OfferGoldenPath')) {
    if (-not $header.Contains($marker)) { throw "StoreDemoExperience.h missing API: $marker" }
}

foreach ($marker in @(
    'store-demo.ini',
    'scene-aurora',
    (From-B64 '546755KD5pe26ZKf'),
    (From-B64 '5LuK5pel5b6F5Yqe'),
    (From-B64 '546755KD5aSp5rCU'),
    (From-B64 '5LiA6ZSu5L2T6aqM'),
    (From-B64 '5ryU56S65qih5byP'),
    'WidgetFixedPreset::GlassClock',
    'ShowWidgetPreviewForPrompt',
    'widget_intent::')) {
    if (-not $demo.Contains($marker)) { throw "StoreDemoExperience.cpp missing marker: $marker" }
}

# API Configuration Center is a provider/profile editor only. Harness is a consumer of the
# selected default Profile and must not require a dedicated launcher/control in the settings UI.
foreach ($marker in @(
    'ApiProfileStore',
    'kDiscoverModelsId',
    'DiscoverModels',
    'kSetDefaultId',
    'SetDefault')) {
    if (-not $ai.Contains($marker)) { throw "DesktopAiSettingsPage missing API Profile marker: $marker" }
}
foreach ($forbidden in @('kOpenHarnessId', 'OpenHarness')) {
    if ($ai.Contains($forbidden)) { throw "Retired Harness/settings coupling returned: $forbidden" }
}
foreach ($marker in @(
    '#include "miaodesk/ApiRuntimeProfile.h"',
    'api_runtime_profile::LoadDefault()',
    'API Configuration Center default profile')) {
    if (-not $harnessSettings.Contains($marker)) { throw "HarnessSettingsBridge missing default Profile consumer marker: $marker" }
}

foreach ($marker in @(
    'TryHandleDemoPrompt',
    (From-B64 '5ryU56S65qih5byP'),
    (From-B64 '5LiA6ZSu5L2T6aqM'),
    'StoreDemoExperience.h')) {
    if (-not $conversation.Contains($marker)) { throw "ConversationPanel missing demo marker: $marker" }
}

foreach ($marker in @('MaybeShowFirstRun', 'kFirstRunTimerId', 'StoreDemoExperience.h')) {
    if (-not $search.Contains($marker)) { throw "SearchWindow missing first-run marker: $marker" }
}

if (-not $cmake.Contains('src/desktop/demo/StoreDemoExperience.cpp')) {
    throw 'CMakeLists.txt must compile StoreDemoExperience.cpp'
}
if (-not $cmake.Contains('src/desktop/widgets/DesktopWidgetController.cpp')) {
    throw 'App target must include DesktopWidgetController for Store Demo clocks.'
}

Write-Host 'Store Demo scope OK: first-run/no-Key golden path remains wired, API settings owns Profiles, and Harness consumes the default Profile without UI coupling.'
