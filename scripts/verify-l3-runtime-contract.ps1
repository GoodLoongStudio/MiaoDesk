$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

$paths = @{
    L3 = Join-Path $root 'src/native/src/L3CliWindow.cpp'
    Pi = Join-Path $root 'src/native/src/PiRuntime.cpp'
    PiTools = Join-Path $root 'src/native/src/PiNativeToolsExtension.cpp'
    PiE2E = Join-Path $root 'scripts/pi-agent-e2e.mjs'
    NativeToolsHeader = Join-Path $root 'src/native/include/turingdesk/NativeTools.h'
    NativeToolIsolation = Join-Path $root 'src/native/src/NativeToolIsolation.cpp'
    Main = Join-Path $root 'src/native/src/main.cpp'
    DesktopTools = Join-Path $root 'src/native/src/DesktopWidgetTools.cpp'
    WidgetStore = Join-Path $root 'src/native/src/DesktopWidgetStore.cpp'
    Harness = Join-Path $root 'src/native/src/HarnessProcessManager.cpp'
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
    'src/native/src/CodexRuntime.cpp',
    'src/native/include/turingdesk/CodexHostBridge.h',
    'src/native/src/CodexHostBridge.cpp',
    'scripts/verify-codex-jsonl-wire.ps1',
    'docs/TURINGDESK-DESIGN-SPEC.md',
    'docs/LEGACY-REDUNDANCY-CLEANUP-PLAN.md',
    'docs/AI-WORKBENCH-CONSOLIDATION-PLAN.md',
    'docs/THIRD-PARTY-EVERYTHING.md',
    '.github/workflows/apply-codex-reconnect-diagnostics.yml',
    '.github/workflows/implement-l4-handoff.yml',
    '.github/workflows/repair-release-doc-contract.yml',
    '.github/workflows/repair-canonical-l3-boundary.yml',
    '.github/workflows/repair-l3-direct-runtime.yml'
)
foreach ($relative in $forbiddenPaths) {
    if (Test-Path (Join-Path $root $relative)) {
        throw "Retired architecture artifact or self-modifying automation must stay removed: $relative"
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

# Forward-only AI architecture: Pi first, Direct API fallback only.
foreach ($marker in @(
    '#include "turingdesk/PiRuntime.h"',
    'ActiveRuntime::Pi',
    'gPiRuntime',
    'state.pi->AskAsync',
    'StartDirectFallback',
    'state.agent->AskAsync',
    'route: primary pi start',
    'fallback: direct api start',
    'primary=Pi Agent -> Provider API; fallback=Direct API'
)) {
    if (-not $l3.Contains($marker)) { throw "Pi-first AI marker missing: $marker" }
}
if ($l3.Contains('ActiveRuntime::Codex') -or $l3.Contains('CodexRuntime')) {
    throw 'Architecture regression: retired Codex runtime returned to the AI UI.'
}

# Normal product UI must not expose implementation routing. /runtime remains diagnostics only.
foreach ($marker in @(
    'state.transcriptPrefix += L"[Runtime] "',
    'state.transcriptPrefix += L"[Fallback]',
    'RuntimeExecutionLabel('
)) {
    if ($l3.Contains($marker)) { throw "Internal runtime branding leaked into the normal AI transcript: $marker" }
}
foreach ($marker in @('UserFacingLocalReply', 'ShowL3CliWindow', '/runtime')) {
    if (-not $l3.Contains($marker)) { throw "AI/diagnostics boundary marker missing: $marker" }
}

# Pi runtime, product extension, Desktop Control bridge and widget store must be in the ordinary binary.
foreach ($marker in @(
    'src/PiRuntime.cpp',
    'src/PiNativeToolsExtension.cpp',
    'src/NativeTools.cpp',
    'src/DesktopWidgetStore.cpp',
    'src/DesktopWidgetTools.cpp'
)) {
    if (-not $cmake.Contains($marker)) { throw "TuringDesk build graph marker missing: $marker" }
}
foreach ($marker in @('src/CodexRuntime.cpp', 'src/CodexHostBridge.cpp', 'TuringDeskCodexJsonlContractCheck')) {
    if ($cmake.Contains($marker)) { throw "Retired Codex build marker is still active: $marker" }
}

# Provider-neutral Pi RPC host and diagnostics.
foreach ($marker in @(
    '@earendil-works',
    '--mode rpc',
    'PI_CODING_AGENT_DIR',
    'openai-completions',
    'openai-responses',
    'anthropic-messages',
    'google-generative-ai',
    'RuntimeLogPath(L"pi-runtime.log")',
    '\"type\":\"prompt\"',
    '\"type\":\"abort\"',
    '\"type\":\"new_session\"',
    '\"type\":\"agent_settled\"',
    'auto url = agent.CurrentApiUrl();',
    'std::hash<std::wstring>{}(setup.apiKey)',
    'turingdesk-local'
)) {
    if (-not $pi.Contains($marker)) { throw "Pi runtime contract marker missing: $marker" }
}
foreach ($marker in @('providerId == L"deepseek"', 'providerId) == L"deepseek"')) {
    if ($pi.Contains($marker)) { throw "Pi transport must not be hard-wired to a provider brand: $marker" }
}
if ($pi.Contains('return SearchExecutable(L"node.exe")')) {
    throw 'Pi Runtime must not fall back to a system Node installation.'
}

# Product-specific tools are loaded through one Pi extension and one isolated native worker.
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
foreach ($marker in @(
    'pi.registerTool({',
    'pi.setActiveTools',
    'TURINGDESK_NATIVE_TOOL_HOST',
    '--native-tool-worker'
) + $desktopToolNames) {
    if (-not $piTools.Contains($marker)) { throw "Pi desktop tools extension marker missing: $marker" }
}

# Generic C++ Agent tools must not return through the product-specific bridge.
foreach ($marker in @('ppt_create', 'file_create', 'folder_list', 'file_open')) {
    if ($nativeToolsHeader.Contains($marker)) { throw "Generic C++ tool must not be exposed by NativeTools: $marker" }
    $workerMarker = 'tool == "' + $marker + '"'
    if ($main.Contains($workerMarker)) { throw "Generic C++ tool must not be allowed through the Pi native worker: $marker" }
}

# The native worker must delegate Desktop Control tools through the dedicated validated bridge.
foreach ($marker in @(
    'EnsurePiNativeToolsExtension',
    'IsAllowedPiNativeTool',
    'turingdesk::IsDesktopControlTool',
    'turingdesk::ExecuteDesktopControlTool',
    'settings_open',
    'wallpaper_create_web_package',
    'wallpaper_validate_package'
)) {
    if (-not $main.Contains($marker)) { throw "Pi native worker marker missing: $marker" }
}
foreach ($marker in @(
    'IsDesktopControlTool',
    'ExecuteDesktopControlTool',
    'WallpaperStateGet',
    'WallpaperApplyWebPackage',
    'WidgetCreate',
    'WidgetUpdate',
    'WidgetRemove',
    'WidgetList'
) + $desktopToolNames[3..8]) {
    if (-not $desktopTools.Contains($marker)) { throw "Desktop Control bridge marker missing: $marker" }
}
foreach ($marker in @(
    'DesktopWidgetStore::SelfTest',
    'CreateManagedWeb',
    'UpdateManagedHtml'
)) {
    if (-not $widgetStore.Contains($marker)) { throw "Desktop widget persistence marker missing: $marker" }
}
if (-not $main.Contains('DesktopWidgetStore::SelfTest')) {
    throw 'Native self-test must cover the DesktopWidgetStore.'
}
if (-not $nativeToolIsolation.Contains('RuntimeLogPath(L"pi-runtime.log")')) {
    throw 'Native tool worker diagnostics must route to pi-runtime.log.'
}
if ($nativeToolIsolation.Contains('codex-runtime.log') -or $nativeToolsHeader.Contains('Codex')) {
    throw 'Retired Codex native-tool wording/log routing returned.'
}

# ARM64 E2E must use the generated Pi extension, handshake RPC and execute real built-in and native tools.
if (-not $arm.Contains('scripts\pi-agent-e2e.mjs')) {
    throw 'ARM64 workflow must execute the dedicated Pi Agent E2E script.'
}
foreach ($marker in @(
    'get_state',
    'agent_settled',
    'settings_open',
    'wallpaper_create_web_package',
    'wallpaper_validate_package',
    'TURINGDESK_NATIVE_TOOL_HOST',
    'manifest.json',
    'PI_WRITE_OK',
    'PI_SHELL_OK'
)) {
    if (-not $piE2E.Contains($marker)) { throw "Pi Agent E2E marker missing: $marker" }
}

# Current E2E executes the wallpaper native tool; new Desktop Control names must at least remain exposed.
foreach ($marker in $desktopToolNames[3..8]) {
    if (-not $piTools.Contains($marker)) { throw "Desktop Control tool disappeared from Pi extension: $marker" }
}

# The updater owns full-package staging and exact validated-build RuntimeBundle alignment.
foreach ($marker in @(
    'Materialize-Runtime',
    'Test-StagedPackage',
    '.installed-build-sha',
    '-SkipGozServiceInstall',
    'NativeTest.next-',
    'Materialize-Runtime $next $validated.BuildSha',
    'git -C $runtimeRepo checkout $BuildSha',
    'RuntimeBundle revision mismatch'
)) {
    if (-not $update.Contains($marker)) { throw "ARM64 updater marker missing: $marker" }
}

# The deploy wrapper validates current main first, then delegates installation to the updater.
foreach ($marker in @(
    'Ensure-ValidatedCurrentMain',
    'update-turingdesk-arm64.ps1',
    'powershell.exe',
    '-File $Updater'
)) {
    if (-not $deploy.Contains($marker)) { throw "ARM64 deploy wrapper marker missing: $marker" }
}
if ($deploy.Contains('Assert-DeployedPiRuntime')) {
    throw 'Fresh deployment must not depend on an already-installed Pi runtime.'
}

foreach ($script in @($deploy, $update, $prepare)) {
    foreach ($legacyText in @('Codex CLI', 'Codex Relay', 'Codex-first')) {
        if ($script.Contains($legacyText)) { throw "Deployment/update surface contains retired runtime branding: $legacyText" }
    }
}
if (-not $prepare.Contains('TuringDesk ARM64 RuntimeBundle is ready.')) {
    throw 'Runtime preparation script must report a generic TuringDesk RuntimeBundle status.'
}

# One-click entrypoints must remain ASCII-safe and free of fragile cmd-to-PowerShell escaping.
foreach ($marker in @('verify-windows-powershell-compat.ps1', 'ASCII-only', 'ParseFile')) {
    if (-not $windowsCompat.Contains($marker)) { throw "Windows PowerShell compatibility guard marker missing: $marker" }
}
foreach ($legacyText in @('^|', 'Codex-first', 'Codex CLI', 'Codex Relay')) {
    if ($updateCmd.Contains($legacyText) -or $deployCmd.Contains($legacyText)) {
        throw "One-click entrypoint contains retired or fragile text: $legacyText"
    }
}
foreach ($marker in @('update-turingdesk-arm64.ps1', '$env:TD_UPDATE_URL', '$env:TD_UPDATER', '-File "%TD_UPDATER%"')) {
    if (-not $updateCmd.Contains($marker)) { throw "One-click updater marker missing: $marker" }
}
if (-not $deployCmd.Contains('scripts\deploy-native-arm64.ps1')) {
    throw 'One-click deploy must delegate to the current deploy wrapper.'
}

# Harness stays local and never opens an external browser itself.
$requiredHarnessArgs = 'constexpr wchar_t kHarnessArgs[] = L"web --host 127.0.0.1 --port 3080 --no-open";'
if (-not $harness.Contains($requiredHarnessArgs)) {
    throw 'Harness launch arguments must be loopback-only and include --no-open.'
}

# Both Windows workflows must run architecture and Windows PowerShell compatibility guards.
foreach ($workflow in @($arm, $x64)) {
    if (-not $workflow.Contains('verify-l3-runtime-contract.ps1')) {
        throw 'Cloud build is missing the AI runtime contract guard.'
    }
    if (-not $workflow.Contains('verify-windows-powershell-compat.ps1')) {
        throw 'Cloud build is missing the Windows PowerShell 5.1 compatibility guard.'
    }
}

# Active AI architecture docs must agree on Pi-first and Direct fallback.
foreach ($doc in @($product, $native, $contract)) {
    if (-not $doc.Contains('Pi')) { throw 'Current AI architecture documentation is missing Pi runtime.' }
    if (-not $doc.Contains('Direct Model')) { throw 'Current AI architecture documentation is missing Direct Model fallback.' }
    if ($doc.Contains('Codex CLI')) { throw 'Current architecture documentation still contains the retired Codex CLI design.' }
}

# Desktop product docs must agree on the composition model and AI/Widget control plane.
foreach ($doc in @($product, $native, $contract, $desktopComposition, $wallpaperParity)) {
    foreach ($marker in @('Widget', 'Desktop Control')) {
        if (-not $doc.Contains($marker)) { throw "Desktop composition documentation marker missing: $marker" }
    }
}
if (-not $desktopComposition.Contains('Wallpaper Layer') -or
    -not $desktopComposition.Contains('Widget Layer') -or
    -not $desktopComposition.Contains('Control Layer')) {
    throw 'Desktop Composition architecture must define Wallpaper, Widget and Control layers.'
}
if (-not $wallpaperParity.Contains('Wallpaper Engine-class') -or
    -not $wallpaperParity.Contains('AI desktop control')) {
    throw 'Wallpaper parity roadmap must keep Wallpaper Engine-class depth and AI desktop control.'
}
foreach ($marker in $desktopToolNames) {
    if (-not $contract.Contains($marker)) { throw "Pi runtime contract is missing current Desktop Tool: $marker" }
}

Write-Host 'Runtime contract OK: Pi-first AI, Direct fallback only, Desktop Composition + Widget control plane, isolated native product tools, validated updater, real Windows E2E.'
