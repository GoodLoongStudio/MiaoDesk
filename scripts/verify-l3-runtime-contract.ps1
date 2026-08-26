$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

$paths = @{
    L3 = Join-Path $root 'src/native/src/ui/ai/L3CliWindow.cpp'
    Pi = Join-Path $root 'src/native/src/ai/pi/PiRuntime.cpp'
    PiTools = Join-Path $root 'src/native/src/ai/pi/PiNativeToolsExtension.cpp'
    PiE2E = Join-Path $root 'scripts/pi-agent-e2e.mjs'
    NativeToolsHeader = Join-Path $root 'src/native/include/turingdesk/NativeTools.h'
    NativeToolIsolation = Join-Path $root 'src/native/src/ai/tools/NativeToolIsolation.cpp'
    Main = Join-Path $root 'src/native/src/app/main.cpp'
    DesktopTools = Join-Path $root 'src/native/src/ai/tools/DesktopWidgetTools.cpp'
    WidgetStore = Join-Path $root 'src/native/src/desktop/widgets/DesktopWidgetStore.cpp'
    Harness = Join-Path $root 'src/native/src/harness/HarnessProcessManager.cpp'
    CMake = Join-Path $root 'src/native/CMakeLists.txt'
    Product = Join-Path $root 'docs/TURINGDESK-PRODUCT-BASELINE.md'
    Native = Join-Path $root 'docs/TURINGDESK-NATIVE-TECH-BASELINE.md'
    Contract = Join-Path $root 'docs/L3-PI-RUNTIME-CONTRACT.md'
    DesktopComposition = Join-Path $root 'docs/DESKTOP_COMPOSITION_ARCHITECTURE.md'
    WallpaperParity = Join-Path $root 'docs/WALLPAPER_ENGINE_PARITY.md'
    Deploy = Join-Path $root 'scripts/deploy-native-arm64.ps1'
    Update = Join-Path $root 'scripts/update-turingdesk-arm64.ps1'
    Prepare = Join-Path $root 'scripts/prepare-third-party-runtime-arm64.ps1'
    WindowsCompat = Join-Path $root 'scripts/verify-windows-powershell-compat.ps1'
    UpdateCmd = Join-Path $root 'UPDATE-TURINGDESK.cmd'
    DeployCmd = Join-Path $root 'DEPLOY-NATIVE-ARM64.cmd'
    Arm = Join-Path $root '.github/workflows/native-search-windows.yml'
    X64 = Join-Path $root '.github/workflows/native-x64-validation.yml'
}

foreach ($entry in $paths.GetEnumerator()) {
    if (-not (Test-Path $entry.Value -PathType Leaf)) {
        throw "Required current architecture input missing: $($entry.Value)"
    }
}

$forbiddenPaths = @(
    'legacy',
    'docs/L3-CODEX-RUNTIME-CONTRACT.md',
    'src/native/include/turingdesk/CodexRuntime.h',
    'src/native/include/turingdesk/CodexHostBridge.h',
    'scripts/verify-codex-jsonl-wire.ps1',
    'docs/TURINGDESK-DESIGN-SPEC.md',
    'docs/LEGACY-REDUNDANCY-CLEANUP-PLAN.md',
    'docs/AI-WORKBENCH-CONSOLIDATION-PLAN.md'
)
foreach ($relative in $forbiddenPaths) {
    if (Test-Path (Join-Path $root $relative)) {
        throw "Retired architecture artifact must stay removed: $relative"
    }
}

$l3 = Get-Content $paths.L3 -Raw
$pi = Get-Content $paths.Pi -Raw
$piTools = Get-Content $paths.PiTools -Raw
$piE2E = Get-Content $paths.PiE2E -Raw
$nativeToolsHeader = Get-Content $paths.NativeToolsHeader -Raw
$nativeToolIsolation = Get-Content $paths.NativeToolIsolation -Raw
$main = Get-Content $paths.Main -Raw
$desktopTools = Get-Content $paths.DesktopTools -Raw
$widgetStore = Get-Content $paths.WidgetStore -Raw
$harness = Get-Content $paths.Harness -Raw
$cmake = Get-Content $paths.CMake -Raw
$product = Get-Content $paths.Product -Raw
$native = Get-Content $paths.Native -Raw
$contract = Get-Content $paths.Contract -Raw
$desktopComposition = Get-Content $paths.DesktopComposition -Raw
$wallpaperParity = Get-Content $paths.WallpaperParity -Raw
$deploy = Get-Content $paths.Deploy -Raw
$update = Get-Content $paths.Update -Raw
$prepare = Get-Content $paths.Prepare -Raw
$windowsCompat = Get-Content $paths.WindowsCompat -Raw
$updateCmd = Get-Content $paths.UpdateCmd -Raw
$deployCmd = Get-Content $paths.DeployCmd -Raw
$arm = Get-Content $paths.Arm -Raw
$x64 = Get-Content $paths.X64 -Raw

foreach ($marker in @(
    '#include "turingdesk/PiRuntime.h"',
    'ActiveRuntime::Pi',
    'gPiRuntime',
    'state.pi->AskAsync',
    'StartDirectFallback',
    'state.agent->AskAsync',
    'route: primary pi start',
    'fallback: direct api start')) {
    if (-not $l3.Contains($marker)) { throw "Pi-first AI marker missing: $marker" }
}
if ($l3.Contains('ActiveRuntime::Codex') -or $l3.Contains('CodexRuntime')) {
    throw 'Architecture regression: retired Codex runtime returned to the AI UI.'
}
foreach ($marker in @('UserFacingLocalReply', 'ShowL3CliWindow', '/runtime')) {
    if (-not $l3.Contains($marker)) { throw "AI/diagnostics boundary marker missing: $marker" }
}

# The terminal-style AI window is retired. Keep this guard in the runtime contract because
# the UI is the entry point to the Pi-first route and must not silently regress to a second
# legacy presentation path while M3/M4 work continues.
foreach ($marker in @(
    'TuringDesk.Native.ConversationPanel',
    'ConversationState',
    'kSendId',
    'SetBusyVisual')) {
    if (-not $l3.Contains($marker)) { throw "Conversation Panel marker missing: $marker" }
}
foreach ($forbidden in @(
    'TuringDesk.Native.L3CliWindow',
    'kCliClass',
    'Consolas',
    '#include "turingdesk/ModelSettingsWindow.h"',
    'kSettingsId')) {
    if ($l3.Contains($forbidden)) { throw "Retired terminal AI UI marker returned: $forbidden" }
}

foreach ($marker in @(
    'src/ai/pi/PiRuntime.cpp',
    'src/ai/pi/PiNativeToolsExtension.cpp',
    'src/ai/tools/NativeTools.cpp',
    'src/desktop/widgets/DesktopWidgetStore.cpp',
    'src/ai/tools/DesktopWidgetTools.cpp')) {
    if (-not $cmake.Contains($marker)) { throw "TuringDesk build graph marker missing: $marker" }
}
foreach ($marker in @('CodexRuntime.cpp', 'CodexHostBridge.cpp', 'TuringDeskCodexJsonlContractCheck')) {
    if ($cmake.Contains($marker)) { throw "Retired Codex build marker is still active: $marker" }
}

foreach ($marker in @(
    '@earendil-works', '--mode rpc', 'PI_CODING_AGENT_DIR',
    'openai-completions', 'openai-responses', 'anthropic-messages', 'google-generative-ai',
    'RuntimeLogPath(L"pi-runtime.log")', '\"type\":\"prompt\"', '\"type\":\"abort\"',
    '\"type\":\"new_session\"', '\"type\":\"agent_settled\"', 'turingdesk-local')) {
    if (-not $pi.Contains($marker)) { throw "Pi runtime contract marker missing: $marker" }
}
if ($pi.Contains('return SearchExecutable(L"node.exe")')) {
    throw 'Pi Runtime must not fall back to a system Node installation.'
}

$desktopToolNames = @(
    'settings_open',
    'wallpaper_create_web_package',
    'wallpaper_validate_package',
    'wallpaper_state_get',
    'wallpaper_apply_web_package',
    'desktop_widget_create_web',
    'desktop_widget_update',
    'desktop_widget_remove',
    'desktop_widget_list'
)
$piToolMarkers = @('pi.registerTool({', 'pi.setActiveTools', 'TURINGDESK_NATIVE_TOOL_HOST', '--native-tool-worker') + $desktopToolNames
foreach ($marker in $piToolMarkers) {
    if (-not $piTools.Contains($marker)) { throw "Pi desktop tools extension marker missing: $marker" }
}

foreach ($marker in @('ppt_create', 'file_create', 'folder_list', 'file_open')) {
    if ($nativeToolsHeader.Contains($marker)) { throw "Generic C++ tool must not be exposed by NativeTools: $marker" }
}
foreach ($marker in @('EnsurePiNativeToolsExtension', 'IsAllowedPiNativeTool', 'turingdesk::IsDesktopControlTool', 'turingdesk::ExecuteDesktopControlTool')) {
    if (-not $main.Contains($marker)) { throw "Pi native worker marker missing: $marker" }
}
foreach ($marker in @('IsDesktopControlTool', 'ExecuteDesktopControlTool', 'WallpaperStateGet', 'WallpaperApplyWebPackage', 'WidgetCreate', 'WidgetUpdate', 'WidgetRemove', 'WidgetList')) {
    if (-not $desktopTools.Contains($marker)) { throw "Desktop Control bridge marker missing: $marker" }
}
foreach ($marker in @('DesktopWidgetStore::SelfTest', 'CreateManagedWeb', 'UpdateManagedHtml')) {
    if (-not $widgetStore.Contains($marker)) { throw "Desktop widget persistence marker missing: $marker" }
}
if (-not $main.Contains('DesktopWidgetStore::SelfTest')) { throw 'Native self-test must cover DesktopWidgetStore.' }
if (-not $nativeToolIsolation.Contains('RuntimeLogPath(L"pi-runtime.log")')) { throw 'Native tool worker diagnostics must route to pi-runtime.log.' }

if (-not $arm.Contains('scripts\pi-agent-e2e.mjs')) { throw 'ARM64 workflow must execute Pi Agent E2E.' }
foreach ($marker in @('get_state', 'agent_settled', 'settings_open', 'wallpaper_create_web_package', 'wallpaper_validate_package', 'TURINGDESK_NATIVE_TOOL_HOST', 'PI_WRITE_OK', 'PI_SHELL_OK')) {
    if (-not $piE2E.Contains($marker)) { throw "Pi Agent E2E marker missing: $marker" }
}

foreach ($marker in @('Materialize-Runtime', 'Test-StagedPackage', '.installed-build-sha', 'Materialize-Runtime $next $validated.BuildSha', 'RuntimeBundle revision mismatch')) {
    if (-not $update.Contains($marker)) { throw "ARM64 updater marker missing: $marker" }
}
foreach ($marker in @('Ensure-ValidatedCurrentMain', 'update-turingdesk-arm64.ps1', 'powershell.exe', '-File $Updater')) {
    if (-not $deploy.Contains($marker)) { throw "ARM64 deploy wrapper marker missing: $marker" }
}
foreach ($script in @($deploy, $update, $prepare)) {
    foreach ($legacyText in @('Codex CLI', 'Codex Relay', 'Codex-first')) {
        if ($script.Contains($legacyText)) { throw "Deployment/update surface contains retired runtime branding: $legacyText" }
    }
}

foreach ($marker in @('verify-windows-powershell-compat.ps1', 'ASCII-only', 'ParseFile')) {
    if (-not $windowsCompat.Contains($marker)) { throw "Windows PowerShell compatibility guard marker missing: $marker" }
}
if (-not $deployCmd.Contains('scripts\deploy-native-arm64.ps1')) { throw 'One-click deploy must delegate to the current deploy wrapper.' }

$requiredHarnessArgs = 'constexpr wchar_t kHarnessArgs[] = L"web --host 127.0.0.1 --port 3080 --no-open";'
if (-not $harness.Contains($requiredHarnessArgs)) {
    throw 'Harness launch arguments must be loopback-only and include --no-open.'
}

foreach ($workflow in @($arm, $x64)) {
    if (-not $workflow.Contains('verify-l3-runtime-contract.ps1')) { throw 'Cloud build is missing the AI runtime contract guard.' }
    if (-not $workflow.Contains('verify-windows-powershell-compat.ps1')) { throw 'Cloud build is missing Windows PowerShell compatibility guard.' }
}

foreach ($doc in @($product, $native, $contract)) {
    if (-not $doc.Contains('Pi')) { throw 'Current AI architecture documentation is missing Pi runtime.' }
    if (-not $doc.Contains('Direct Model')) { throw 'Current AI architecture documentation is missing Direct Model fallback.' }
    if ($doc.Contains('Codex CLI')) { throw 'Current architecture documentation still contains retired Codex CLI design.' }
}
foreach ($doc in @($product, $native, $contract, $desktopComposition, $wallpaperParity)) {
    foreach ($marker in @('Widget', 'Desktop Control')) {
        if (-not $doc.Contains($marker)) { throw "Desktop composition documentation marker missing: $marker" }
    }
}
foreach ($marker in $desktopToolNames) {
    if (-not $contract.Contains($marker)) { throw "Pi runtime contract is missing current Desktop Tool: $marker" }
}

Write-Host 'Runtime contract OK: Pi-first AI, Conversation Panel, Direct fallback, Desktop Control/Widget plane and native source layout remain intact.'
