$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$l3Path = Join-Path $root 'src/native/src/L3CliWindow.cpp'
$codexPath = Join-Path $root 'src/native/src/CodexRuntime.cpp'
$cmakePath = Join-Path $root 'src/native/CMakeLists.txt'
$armWorkflowPath = Join-Path $root '.github/workflows/native-search-windows.yml'

foreach ($path in @($l3Path, $codexPath, $cmakePath)) {
    if (-not (Test-Path $path)) { throw "L3 runtime contract input missing: $path" }
}

$l3 = Get-Content $l3Path -Raw
$codex = Get-Content $codexPath -Raw
$cmake = Get-Content $cmakePath -Raw

$requiredL3 = @(
    '#include "turingdesk/CodexRuntime.h"',
    'ActiveRuntime::Codex',
    'gCodexRuntime',
    'state.codex->AskAsync',
    'state.agent->AskAsync',
    'Codex CLI → Relay/API',
    '[Fallback] Codex CLI 失败，已切换 Direct API',
    'route: primary codex start',
    'fallback: direct api start',
    'l3-runtime.log',
    'codex-runtime.log'
)
foreach ($marker in $requiredL3) {
    if (-not $l3.Contains($marker)) { throw "L3 Codex-first contract marker missing: $marker" }
}

$forbiddenL3 = @(
    'DirectToolRuntime',
    'WantsNativeTools',
    'L3 外部 Agent/Harness/Relay：不参与',
    '普通对话：Direct Model Runtime · SSE 流式'
)
foreach ($marker in $forbiddenL3) {
    if ($l3.Contains($marker)) { throw "Retired L3 routing marker returned: $marker" }
}

if (-not $cmake.Contains('src/CodexRuntime.cpp')) {
    throw 'CodexRuntime.cpp must be compiled into TuringDesk.'
}
if (-not $cmake.Contains('src/NativeTools.cpp')) {
    throw 'NativeTools.cpp must remain compiled for Codex dynamic tools.'
}
if ($cmake.Contains('src/DirectAgentRuntimeV2.cpp')) {
    throw 'Retired DirectToolRuntime must not be compiled into the default L3 route.'
}
if (-not $cmake.Contains('TuringDeskL3ContractCheck')) {
    throw 'The build must run the L3 runtime contract guard before compiling TuringDesk.'
}

$requiredCodex = @(
    'OpenAiResponsesBase',
    'ChatCompletionsBase',
    'setup.relayRequired = true',
    'codex-runtime.log',
    'relay: readiness failed',
    'app-server: initialize',
    'session: failed'
)
foreach ($marker in $requiredCodex) {
    if (-not $codex.Contains($marker)) { throw "Codex runtime diagnostic/transport marker missing: $marker" }
}

# The primary transport is capability-based, not brand-based. Provider-specific
# compatibility tweaks may exist, but routing must never require a DeepSeek id.
foreach ($marker in @('providerId == L"deepseek"', 'providerId) == L"deepseek"')) {
    if ($codex.Contains($marker)) { throw "Codex routing must not be hard-wired to DeepSeek: $marker" }
}

if (Test-Path $armWorkflowPath) {
    $workflow = Get-Content $armWorkflowPath -Raw
    if ($workflow.Contains('Remove-Item build/package/Codex -Recurse')) {
        throw 'ARM64 artifact must not delete bundled Codex CLI after validation.'
    }
    if ($workflow.Contains('Remove-Item build/package/CodexRelay -Recurse')) {
        throw 'ARM64 artifact must not delete bundled Codex Relay after validation.'
    }
}

Write-Host 'L3 runtime contract OK: Codex CLI -> Relay/API, Direct API fallback, diagnostics enabled.'
