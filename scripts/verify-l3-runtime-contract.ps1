$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

$paths = @{
    Conversation = Join-Path $root 'src/native/src/ui/ai/ConversationPanel.cpp'
    ConversationImpl = Join-Path $root 'src/native/src/ui/ai/ConversationPanelImpl.inc'
    ConversationHeader = Join-Path $root 'src/native/include/turingdesk/ConversationPanel.h'
    LegacyHeader = Join-Path $root 'src/native/include/turingdesk/L3CliWindow.h'
    Pi = Join-Path $root 'src/native/src/ai/pi/PiRuntime.cpp'
    PiTools = Join-Path $root 'src/native/src/ai/pi/PiNativeToolsExtension.cpp'
    PiE2E = Join-Path $root 'scripts/pi-agent-e2e.mjs'
    NativeToolsHeader = Join-Path $root 'src/native/include/turingdesk/NativeTools.h'
    NativeToolIsolation = Join-Path $root 'src/native/src/ai/tools/NativeToolIsolation.cpp'
    Main = Join-Path $root 'src/native/src/app/main.cpp'
    DesktopTools = Join-Path $root 'src/native/src/ai/tools/DesktopWidgetTools.cpp'
    GeneratedPreview = Join-Path $root 'src/native/src/desktop/preview/GeneratedDesktopPreview.cpp'
    A2UI = Join-Path $root 'src/native/src/ai/a2ui/A2UIParser.cpp'
    WidgetStore = Join-Path $root 'src/native/src/desktop/widgets/DesktopWidgetStore.cpp'
    Harness = Join-Path $root 'src/native/src/harness/HarnessProcessManager.cpp'
    CMake = Join-Path $root 'src/native/CMakeLists.txt'
    Product = Join-Path $root 'docs/TURINGDESK-PRODUCT-BASELINE.md'
    Native = Join-Path $root 'docs/TURINGDESK-NATIVE-TECH-BASELINE.md'
    Contract = Join-Path $root 'docs/L3-PI-RUNTIME-CONTRACT.md'
    DesktopComposition = Join-Path $root 'docs/DESKTOP_COMPOSITION_ARCHITECTURE.md'
    WallpaperParity = Join-Path $root 'docs/WALLPAPER_ENGINE_PARITY.md'
    Sandbox = Join-Path $root 'docs/AI_GENERATED_DESKTOP_SANDBOX.md'
    Schema = Join-Path $root 'docs/schemas/a2ui-widget.schema.json'
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
    if (-not (Test-Path $entry.Value -PathType Leaf)) { throw "Required current architecture input missing: $($entry.Value)" }
}

$forbiddenPaths = @(
    'legacy',
    'src/native/src/ui/ai/L3CliWindow.cpp',
    'docs/L3-CODEX-RUNTIME-CONTRACT.md',
    'src/native/include/turingdesk/CodexRuntime.h',
    'src/native/include/turingdesk/CodexHostBridge.h',
    'scripts/verify-codex-jsonl-wire.ps1',
    'docs/TURINGDESK-DESIGN-SPEC.md',
    'docs/LEGACY-REDUNDANCY-CLEANUP-PLAN.md',
    'docs/AI-WORKBENCH-CONSOLIDATION-PLAN.md'
)
foreach ($relative in $forbiddenPaths) {
    if (Test-Path (Join-Path $root $relative)) { throw "Retired architecture artifact must stay removed: $relative" }
}

$conversation = Get-Content $paths.Conversation -Raw
$l3 = Get-Content $paths.ConversationImpl -Raw
$conversationHeader = Get-Content $paths.ConversationHeader -Raw
$legacyHeader = Get-Content $paths.LegacyHeader -Raw
$pi = Get-Content $paths.Pi -Raw
$piTools = Get-Content $paths.PiTools -Raw
$piE2E = Get-Content $paths.PiE2E -Raw
$nativeToolsHeader = Get-Content $paths.NativeToolsHeader -Raw
$nativeToolIsolation = Get-Content $paths.NativeToolIsolation -Raw
$main = Get-Content $paths.Main -Raw
$desktopTools = Get-Content $paths.DesktopTools -Raw
$generatedPreview = Get-Content $paths.GeneratedPreview -Raw
$a2ui = Get-Content $paths.A2UI -Raw
$widgetStore = Get-Content $paths.WidgetStore -Raw
$harness = Get-Content $paths.Harness -Raw
$cmake = Get-Content $paths.CMake -Raw
$product = Get-Content $paths.Product -Raw
$native = Get-Content $paths.Native -Raw
$contract = Get-Content $paths.Contract -Raw
$desktopComposition = Get-Content $paths.DesktopComposition -Raw
$wallpaperParity = Get-Content $paths.WallpaperParity -Raw
$sandbox = Get-Content $paths.Sandbox -Raw
$schema = Get-Content $paths.Schema -Raw
$deploy = Get-Content $paths.Deploy -Raw
$update = Get-Content $paths.Update -Raw
$prepare = Get-Content $paths.Prepare -Raw
$windowsCompat = Get-Content $paths.WindowsCompat -Raw
$deployCmd = Get-Content $paths.DeployCmd -Raw
$arm = Get-Content $paths.Arm -Raw
$x64 = Get-Content $paths.X64 -Raw

foreach ($marker in @(
    '#include "turingdesk/PiRuntime.h"', 'ActiveRuntime::Pi', 'gPiRuntime', 'state.pi->AskAsync',
    'StartDirectFallback', 'state.agent->AskAsync', 'route: primary pi start', 'fallback: direct api start')) {
    if (-not $l3.Contains($marker)) { throw "Pi-first AI marker missing: $marker" }
}
if ($l3.Contains('ActiveRuntime::Codex') -or $l3.Contains('CodexRuntime')) { throw 'Architecture regression: retired Codex runtime returned to the AI UI.' }
foreach ($marker in @('UserFacingLocalReply', 'ShowL3CliWindow', '/runtime')) {
    if (-not $l3.Contains($marker)) { throw "AI/diagnostics boundary marker missing: $marker" }
}

foreach ($marker in @('#include "turingdesk/ConversationPanel.h"', '#define ShowL3CliWindow ShowConversationPanel', '#include "ConversationPanelImpl.inc"')) {
    if (-not $conversation.Contains($marker)) { throw "Conversation Panel canonical wrapper marker missing: $marker" }
}
foreach ($marker in @('bool ShowConversationPanel(', 'return ShowConversationPanel(')) {
    if (-not $conversationHeader.Contains($marker)) { throw "Conversation Panel canonical API marker missing: $marker" }
}
if (-not $legacyHeader.Contains('#include "turingdesk/ConversationPanel.h"')) { throw 'Legacy L3CliWindow.h must remain a compatibility-only include of ConversationPanel.h.' }
foreach ($marker in @('TuringDesk.Native.ConversationPanel', 'ConversationState', 'kSendId', 'SetBusyVisual')) {
    if (-not $l3.Contains($marker)) { throw "Conversation Panel marker missing: $marker" }
}
foreach ($forbidden in @('TuringDesk.Native.L3CliWindow', 'kCliClass', 'Consolas', '#include "turingdesk/ModelSettingsWindow.h"', 'kSettingsId')) {
    if ($l3.Contains($forbidden)) { throw "Retired terminal AI UI marker returned: $forbidden" }
}

foreach ($marker in @(
    'src/ui/ai/ConversationPanel.cpp', 'src/ai/pi/PiRuntime.cpp', 'src/ai/pi/PiNativeToolsExtension.cpp',
    'src/ai/tools/NativeTools.cpp', 'src/ai/a2ui/A2UIParser.cpp', 'src/desktop/preview/GeneratedDesktopPreview.cpp',
    'src/desktop/widgets/DesktopWidgetStore.cpp', 'src/ai/tools/DesktopWidgetTools.cpp')) {
    if (-not $cmake.Contains($marker)) { throw "TuringDesk build graph marker missing: $marker" }
}
foreach ($marker in @('src/ui/ai/L3CliWindow.cpp', 'CodexRuntime.cpp', 'CodexHostBridge.cpp', 'TuringDeskCodexJsonlContractCheck')) {
    if ($cmake.Contains($marker)) { throw "Retired build marker is still active: $marker" }
}

foreach ($marker in @(
    '@earendil-works', '--mode rpc', 'PI_CODING_AGENT_DIR', 'openai-completions', 'openai-responses',
    'anthropic-messages', 'google-generative-ai', 'RuntimeLogPath(L"pi-runtime.log")',
    '\"type\":\"prompt\"', '\"type\":\"abort\"', '\"type\":\"new_session\"', '\"type\":\"agent_settled\"', 'turingdesk-local',
    '--no-extensions', '--extension', 'agent-tools-v1', 'desktop_preview_examples',
    'persistent desktop Agent', 'MUST call tools first')) {
    if (-not $pi.Contains($marker)) { throw "Pi runtime contract marker missing: $marker" }
}
if ($pi.Contains('--tools read,bash,edit,write,grep,find,ls --append-system-prompt')) {
    throw 'Pi Runtime still uses the built-in-only --tools allowlist that strips extension Agent tools.'
}
if ($pi.Contains('return SearchExecutable(L"node.exe")')) { throw 'Pi Runtime must not fall back to a system Node installation.' }

$piPreviewTools = @(
    'settings_open', 'wallpaper_validate_package', 'wallpaper_state_get', 'desktop_widget_list',
    'desktop_preview_widget', 'desktop_preview_wallpaper', 'desktop_preview_examples'
)
foreach ($marker in @('pi.registerTool({', 'pi.setActiveTools', 'TURINGDESK_NATIVE_TOOL_HOST', '--native-tool-worker') + $piPreviewTools) {
    if (-not $piTools.Contains($marker)) { throw "Pi preview tool marker missing: $marker" }
}
$forbiddenPiMutationTools = @(
    'wallpaper_create_web_package', 'wallpaper_apply_web_package',
    'desktop_widget_create_web', 'desktop_widget_update', 'desktop_widget_remove'
)
foreach ($tool in $forbiddenPiMutationTools) {
    if ($piTools.Contains("`"$tool`"")) { throw "Pi must not expose direct desktop mutation tool: $tool" }
}
foreach ($marker in @('PREVIEW-FIRST', 'native Apply button', 'A2UI JSON only')) {
    if (-not $piTools.Contains($marker)) { throw "Pi desktop safety prompt marker missing: $marker" }
}

foreach ($marker in @('ppt_create', 'file_create', 'folder_list', 'file_open')) {
    if ($nativeToolsHeader.Contains($marker)) { throw "Generic C++ tool must not be exposed by NativeTools header: $marker" }
}
foreach ($marker in @('EnsurePiNativeToolsExtension', 'IsAllowedPiNativeTool', 'preview::IsGeneratedPreviewTool', 'preview::ExecuteGeneratedPreviewTool', 'ExecuteDesktopControlTool')) {
    if (-not $main.Contains($marker)) { throw "Pi native worker safety marker missing: $marker" }
}
foreach ($tool in $forbiddenPiMutationTools) {
    $allowlistArea = $main.Substring($main.IndexOf('bool IsAllowedPiNativeTool'), $main.IndexOf('bool NoProxyContains') - $main.IndexOf('bool IsAllowedPiNativeTool'))
    if ($allowlistArea.Contains("`"$tool`"")) { throw "Native Pi worker allowlist contains direct mutation tool: $tool" }
}

# Internal desktop mutation APIs remain available to the host-owned Apply boundary.
foreach ($marker in @('IsDesktopControlTool', 'ExecuteDesktopControlTool', 'WallpaperApplyWebPackage', 'WidgetCreate', 'WidgetUpdate', 'WidgetRemove', 'WidgetList')) {
    if (-not $desktopTools.Contains($marker)) { throw "Internal Desktop Control bridge marker missing: $marker" }
}
foreach ($marker in @(
    'desktop_preview_widget', 'desktop_preview_wallpaper', 'desktop_preview_examples',
    'ValidateWidgetDocument', 'AI_Generated', 'HandleGeneratedPreviewCopyData',
    'put_DefaultBackgroundColor', 'kApplyButtonId', 'kRejectButtonId',
    'CreateWebWidget', 'ApplyWebPackage', 'CleanupPreview')) {
    if (-not $generatedPreview.Contains($marker)) { throw "Generated desktop sandbox marker missing: $marker" }
}
# A2UI rejects unknown object fields through the parser's HasOnly allowlist.
# The JSON schema independently pins additionalProperties=false.
foreach ($marker in @('HasOnly', 'A2UI', 'ValidateNode', 'count > 32', 'depth > 4', 'Button')) {
    if (-not $a2ui.Contains($marker)) { throw "A2UI parser safety marker missing: $marker" }
}
foreach ($marker in @('additionalProperties', 'Card', 'Text', 'Button', 'Weather', 'List')) {
    if (-not $schema.Contains($marker)) { throw "A2UI schema marker missing: $marker" }
}
foreach ($marker in @('Apply', 'Reject', 'AI_Generated', 'WebView2', 'declarative', 'Aurora Flow', 'Ocean Glass', 'Today Tasks', 'System Pulse')) {
    if (-not $sandbox.Contains($marker)) { throw "AI desktop sandbox documentation marker missing: $marker" }
}

foreach ($marker in @('DesktopWidgetStore::SelfTest', 'CreateManagedWeb', 'UpdateManagedHtml')) {
    if (-not $widgetStore.Contains($marker)) { throw "Desktop widget persistence marker missing: $marker" }
}
if (-not $main.Contains('DesktopWidgetStore::SelfTest')) { throw 'Native self-test must cover DesktopWidgetStore.' }
if (-not $nativeToolIsolation.Contains('RuntimeLogPath(L"pi-runtime.log")')) { throw 'Native tool worker diagnostics must route to pi-runtime.log.' }

if (-not $arm.Contains('scripts\pi-agent-e2e.mjs')) { throw 'ARM64 workflow must execute Pi Agent E2E.' }
foreach ($marker in @('get_state', 'agent_settled', 'desktop_preview_widget', 'desktop_preview_wallpaper', 'desktop_preview_examples', 'forbiddenTools', 'TURINGDESK_NATIVE_TOOL_HOST', 'PI_WRITE_OK', 'PI_SHELL_OK')) {
    if (-not $piE2E.Contains($marker)) { throw "Pi Agent E2E marker missing: $marker" }
}
foreach ($forbidden in $forbiddenPiMutationTools) {
    if (-not $piE2E.Contains($forbidden)) { throw "Pi E2E must explicitly guard forbidden mutation tool: $forbidden" }
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
if (-not $harness.Contains($requiredHarnessArgs)) { throw 'Harness launch arguments must be loopback-only and include --no-open.' }

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

Write-Host 'Runtime contract OK: Pi-first AI, preview-only generated desktop boundary, A2UI validation, host-owned Apply/Reject, Direct fallback and native source layout remain intact.'
