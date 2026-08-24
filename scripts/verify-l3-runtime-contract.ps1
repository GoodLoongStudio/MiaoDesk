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
    Harness = Join-Path $root 'src/native/src/HarnessProcessManager.cpp'
    CMake = Join-Path $root 'src/native/CMakeLists.txt'
    Product = Join-Path $root 'docs/TURINGDESK-PRODUCT-BASELINE.md'
    Native = Join-Path $root 'docs/TURINGDESK-NATIVE-TECH-BASELINE.md'
    Contract = Join-Path $root 'docs/L3-PI-RUNTIME-CONTRACT.md'
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
$harness = Get-Content $paths.Harness -Raw
$cmake = Get-Content $paths.CMake -Raw
$product = Get-Content $paths.Product -Raw
$native = Get-Content $paths.Native -Raw
$contract = Get-Content $paths.Contract -Raw
$deploy = Get-Content $paths.Deploy -Raw
$update = Get-Content $paths.Update -Raw
$prepare = Get-Content $paths.Prepare -Raw
$windowsCompat = Get-Content $paths.WindowsCompat -Raw
$updateCmd = Get-Content $paths.UpdateCmd -Raw
$deployCmd = Get-Content $paths.DeployCmd -Raw
$arm = Get-Content $paths.Arm -Raw
$x64 = Get-Content $paths.X64 -Raw

# Forward-only L3 architecture: Pi first, Direct API fallback only.
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
    if (-not $l3.Contains($marker)) { throw "Pi-first L3 marker missing: $marker" }
}
if ($l3.Contains('ActiveRuntime::Codex') -or $l3.Contains('CodexRuntime')) {
    throw 'Architecture regression: retired Codex runtime returned to L3 UI.'
}

# Normal product UI must not expose implementation routing. /runtime remains an explicit diagnostics surface.
foreach ($marker in @(
    'state.transcriptPrefix += L"[Runtime] "',
    'state.transcriptPrefix += L"[Fallback]',
    'RuntimeExecutionLabel('
)) {
    if ($l3.Contains($marker)) { throw "Internal runtime branding leaked into the normal TuringDesk AI transcript: $marker" }
}
foreach ($marker in @('UserFacingLocalReply', 'ShowL3CliWindow', '/runtime')) {
    if (-not $l3.Contains($marker)) { throw "TuringDesk AI/diagnostics boundary marker missing: $marker" }
}

# PiRuntime, Pi extension bridge and TuringDesk product tools must be part of the ordinary binary.
foreach ($marker in @('src/PiRuntime.cpp', 'src/PiNativeToolsExtension.cpp', 'src/NativeTools.cpp')) {
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

# TuringDesk product-specific tools are loaded through a Pi extension and isolated native worker.
foreach ($marker in @(
    'pi.registerTool({',
    'settings_open',
    'wallpaper_create_web_package',
    'wallpaper_validate_package',
    'pi.setActiveTools',
    'TURINGDESK_NATIVE_TOOL_HOST',
    '--native-tool-worker'
)) {
    if (-not $piTools.Contains($marker)) { throw "Pi native tools extension marker missing: $marker" }
}
foreach ($marker in @('ppt_create', 'file_create', 'folder_list', 'file_open')) {
    if ($nativeToolsHeader.Contains($marker)) { throw "Generic C++ tool must not be exposed by NativeTools: $marker" }
    $workerMarker = 'tool == "' + $marker + '"'
    if ($main.Contains($workerMarker)) { throw "Generic C++ tool must not be allowed through the Pi native worker: $marker" }
}
foreach ($marker in @(
    'EnsurePiNativeToolsExtension',
    'IsAllowedPiNativeTool',
    'settings_open',
    'wallpaper_create_web_package',
    'wallpaper_validate_package'
)) {
    if (-not $main.Contains($marker)) { throw "Pi native worker marker missing: $marker" }
}
if (-not $nativeToolIsolation.Contains('RuntimeLogPath(L"pi-runtime.log")')) {
    throw 'Native tool worker diagnostics must route to pi-runtime.log.'
}
if ($nativeToolIsolation.Contains('codex-runtime.log') -or $nativeToolsHeader.Contains('Codex')) {
    throw 'Retired Codex native-tool wording/log routing returned.'
}

# ARM64 E2E must use the real generated Pi extension, wait for agent_settled and execute a native .tdwall tool.
if (-not $arm.Contains('scripts\pi-agent-e2e.mjs')) {
    throw 'ARM64 workflow must execute the dedicated Pi Agent E2E script.'
}
foreach ($marker in @(
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

# One-click entrypoints must remain simple, ASCII-safe and free of fragile cmd-to-PowerShell escaping.
foreach ($marker in @('verify-windows-powershell-compat.ps1', 'ASCII-only', 'ParseFile')) {
    if (-not $windowsCompat.Contains($marker)) { throw "Windows PowerShell compatibility guard marker missing: $marker" }
}
foreach ($legacyText in @('^|', 'Codex-first', 'Codex CLI', 'Codex Relay')) {
    if ($updateCmd.Contains($legacyText) -or $deployCmd.Contains($legacyText)) {
        throw "One-click entrypoint contains retired or fragile text: $legacyText"
    }
}
if (-not $updateCmd.Contains('update-turingdesk-arm64.ps1') -or -not $updateCmd.Contains('-File "%UPDATER%"')) {
    throw 'One-click updater must directly execute the ASCII-safe updater script.'
}
if (-not $deployCmd.Contains('scripts\deploy-native-arm64.ps1')) {
    throw 'One-click deploy must delegate to the current deploy wrapper.'
}

# Harness stays local and never opens an external browser itself.
$requiredHarnessArgs = 'constexpr wchar_t kHarnessArgs[] = L"web --host 127.0.0.1 --port 3080 --no-open";'
if (-not $harness.Contains($requiredHarnessArgs)) {
    throw 'Harness launch arguments must be loopback-only and include --no-open.'
}

# Both Windows workflows must run the architecture guard and Windows PowerShell compatibility guard.
foreach ($workflow in @($arm, $x64)) {
    if (-not $workflow.Contains('verify-l3-runtime-contract.ps1')) {
        throw 'Cloud build is missing the L3 runtime contract guard.'
    }
    if (-not $workflow.Contains('verify-windows-powershell-compat.ps1')) {
        throw 'Cloud build is missing the Windows PowerShell 5.1 compatibility guard.'
    }
}

# Active architecture docs must agree on Pi-first and Direct fallback.
foreach ($doc in @($product, $native, $contract)) {
    if (-not $doc.Contains('Pi')) { throw 'Current architecture documentation is missing Pi runtime.' }
    if (-not $doc.Contains('Direct Model')) { throw 'Current architecture documentation is missing Direct Model fallback.' }
    if ($doc.Contains('Codex CLI')) { throw 'Current architecture documentation still contains the retired Codex CLI design.' }
}

Write-Host 'L3 runtime contract OK: TuringDesk-only UI, Pi primary, validated full-package updater, PowerShell 5.1-safe entrypoints, provider-neutral routing, settled RPC turns, self-contained Node, native product tools, real E2E, Direct API fallback only.'
