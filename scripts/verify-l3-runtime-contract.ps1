$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$l3Path = Join-Path $root 'src/native/src/L3CliWindow.cpp'
$codexPath = Join-Path $root 'src/native/src/CodexRuntime.cpp'
$cmakePath = Join-Path $root 'src/native/CMakeLists.txt'
$armWorkflowPath = Join-Path $root '.github/workflows/native-search-windows.yml'
$x64WorkflowPath = Join-Path $root '.github/workflows/native-x64-validation.yml'
$deployCmdPath = Join-Path $root 'DEPLOY-NATIVE-ARM64.cmd'
$deployPs1Path = Join-Path $root 'scripts/deploy-native-arm64.ps1'
$productBaselinePath = Join-Path $root 'docs/TURINGDESK-PRODUCT-BASELINE.md'
$nativeBaselinePath = Join-Path $root 'docs/TURINGDESK-NATIVE-TECH-BASELINE.md'
$contractDocPath = Join-Path $root 'docs/L3-CODEX-RUNTIME-CONTRACT.md'
$readmePath = Join-Path $root 'README.md'
$obsoletePlanPath = Join-Path $root 'docs/AI-WORKBENCH-CONSOLIDATION-PLAN.md'

$requiredFiles = @(
    $l3Path, $codexPath, $cmakePath, $armWorkflowPath, $x64WorkflowPath,
    $deployCmdPath, $deployPs1Path, $productBaselinePath, $nativeBaselinePath,
    $contractDocPath, $readmePath
)
foreach ($path in $requiredFiles) {
    if (-not (Test-Path $path -PathType Leaf)) { throw "L3 runtime contract input missing: $path" }
}
if (Test-Path $obsoletePlanPath) {
    throw 'Superseded AI-WORKBENCH-CONSOLIDATION-PLAN.md must stay removed; it encoded the retired Direct-Model-first L3 architecture.'
}

$l3 = Get-Content $l3Path -Raw
$codex = Get-Content $codexPath -Raw
$cmake = Get-Content $cmakePath -Raw
$armWorkflow = Get-Content $armWorkflowPath -Raw
$x64Workflow = Get-Content $x64WorkflowPath -Raw
$deployCmd = Get-Content $deployCmdPath -Raw
$deployPs1 = Get-Content $deployPs1Path -Raw
$productBaseline = Get-Content $productBaselinePath -Raw
$nativeBaseline = Get-Content $nativeBaselinePath -Raw
$contractDoc = Get-Content $contractDocPath -Raw
$readme = Get-Content $readmePath -Raw

# 1) Runtime source: Codex is primary, Direct API exists only as fallback.
$requiredL3 = @(
    '#include "turingdesk/CodexRuntime.h"', 'ActiveRuntime::Codex', 'gCodexRuntime',
    'state.codex->AskAsync', 'state.agent->AskAsync', 'Codex CLI → Relay/API',
    '[Fallback] Codex CLI 失败，已切换 Direct API', 'route: primary codex start',
    'fallback: direct api start', 'l3-runtime.log', 'codex-runtime.log'
)
foreach ($marker in $requiredL3) {
    if (-not $l3.Contains($marker)) { throw "L3 Codex-first contract marker missing: $marker" }
}
$forbiddenL3 = @(
    'DirectToolRuntime', 'WantsNativeTools', 'L3 外部 Agent/Harness/Relay：不参与',
    '普通对话：Direct Model Runtime · SSE 流式',
    'Ordinary L3 must not depend on external Agent/Harness runtime marker'
)
foreach ($marker in $forbiddenL3) {
    if ($l3.Contains($marker)) { throw "Retired L3 routing marker returned: $marker" }
}

# 2) Build graph: CodexRuntime and NativeTools must be in TuringDesk.
foreach ($marker in @('src/CodexRuntime.cpp', 'src/NativeTools.cpp', 'TuringDeskL3ContractCheck', 'add_dependencies(TuringDesk TuringDeskL3ContractCheck)')) {
    if (-not $cmake.Contains($marker)) { throw "CMake L3 build contract marker missing: $marker" }
}
if ($cmake.Contains('src/DirectAgentRuntimeV2.cpp')) {
    throw 'Retired DirectToolRuntime must not be compiled into the default L3 route.'
}

# 3) Codex transport and diagnostics must stay provider-capability based.
foreach ($marker in @(
    'OpenAiResponsesBase', 'ChatCompletionsBase', 'setup.relayRequired = true',
    'codex-runtime.log', 'relay: readiness failed', 'app-server: initialize',
    'thread/start', 'turn/start', 'session: failed'
)) {
    if (-not $codex.Contains($marker)) { throw "Codex runtime diagnostic/transport marker missing: $marker" }
}
foreach ($marker in @('providerId == L"deepseek"', 'providerId) == L"deepseek"')) {
    if ($codex.Contains($marker)) { throw "Codex routing must not be hard-wired to DeepSeek: $marker" }
}

# 4) Cloud build: x64 and ARM64 must both run the same guard before Configure/Build.
foreach ($workflow in @($armWorkflow, $x64Workflow)) {
    foreach ($marker in @('Verify L3 Codex-first runtime contract', '.\scripts\verify-l3-runtime-contract.ps1')) {
        if (-not $workflow.Contains($marker)) { throw "Cloud build is missing L3 contract guard: $marker" }
    }
}
if ($armWorkflow.Contains('Remove-Item build/package/Codex -Recurse')) {
    throw 'ARM64 artifact must not delete bundled Codex CLI after validation.'
}
if ($armWorkflow.Contains('Remove-Item build/package/CodexRelay -Recurse')) {
    throw 'ARM64 artifact must not delete bundled Codex Relay after validation.'
}
foreach ($marker in @(
    'Validate full Codex CLI runtime', 'Real Codex Relay protocol bridge smoke test',
    'Real Codex app-server through Relay end-to-end smoke test',
    '.\build\package\Codex\codex.exe', '.\build\package\CodexRelay\codex-relay.exe'
)) {
    if (-not $armWorkflow.Contains($marker)) { throw "ARM64 Codex validation marker missing: $marker" }
}

# 5) Local deploy entry points must both enforce the contract and refuse missing Codex/Relay.
foreach ($marker in @('verify-l3-runtime-contract.ps1', 'Codex\codex.exe', 'CodexRelay\codex-relay.exe', 'L3 default route: Codex CLI')) {
    if (-not $deployCmd.Contains($marker)) { throw "Local deploy CMD Codex-first marker missing: $marker" }
}
foreach ($marker in @(
    'Assert-L3RuntimeContract', 'verify-l3-runtime-contract.ps1', 'Assert-DeployedCodexRuntime',
    'Codex\codex.exe', 'CodexRelay\codex-relay.exe', 'ArtifactCodex', 'ArtifactRelay',
    'L3 default route: Codex CLI -> Relay/API; Direct API fallback only'
)) {
    if (-not $deployPs1.Contains($marker)) { throw "Direct deploy PowerShell Codex-first marker missing: $marker" }
}

# 6) Active documentation must agree with code.
foreach ($marker in @('Codex CLI 永远优先', '轻量 Direct Model fallback', 'Chat Completions Provider 通过 Codex Relay', 'Responses Provider 可直接连接')) {
    if (-not $productBaseline.Contains($marker)) { throw "Product baseline L3 marker missing: $marker" }
}
foreach ($marker in @('Codex CLI `app-server --stdio`', 'Codex Relay', 'Direct Model 是 **fallback**', 'Provider 路由按协议能力判断', 'scripts/verify-l3-runtime-contract.ps1')) {
    if (-not $nativeBaseline.Contains($marker)) { throw "Native baseline L3 marker missing: $marker" }
}
foreach ($marker in @('Codex CLI 是 L3 默认主路由', 'Direct Model 只允许作为失败回退', 'Provider 无品牌绑定', 'l3-runtime.log', 'codex-runtime.log', '本地构建契约', '云端 CI 契约')) {
    if (-not $contractDoc.Contains($marker)) { throw "L3 design contract marker missing: $marker" }
}
$forbiddenActiveDocMarkers = @(
    'L3 → WinHTTP direct provider', 'L3 → 本地 HTTP 代理',
    '普通 L3 不启动外部 Agent/Harness',
    '搜索栏 L3 保持轻量，不启动官方工作台，也不启动旧 Runtime'
)
foreach ($doc in @($productBaseline, $nativeBaseline, $contractDoc)) {
    foreach ($marker in $forbiddenActiveDocMarkers) {
        if ($doc.Contains($marker)) { throw "Retired Direct-Model-first architecture text returned to an active baseline: $marker" }
    }
}
foreach ($marker in @('Codex CLI `app-server --stdio`', 'Direct Model Runtime', 'Codex Relay', '--no-open', 'L3-CODEX-RUNTIME-CONTRACT.md')) {
    if (-not $readme.Contains($marker)) { throw "README runtime contract marker missing: $marker" }
}

Write-Host 'L3 runtime contract OK: local + cloud builds and both local deploy entry points enforce Codex CLI -> Relay/API, Direct API fallback, provider-neutral routing, diagnostics and packaging.'
