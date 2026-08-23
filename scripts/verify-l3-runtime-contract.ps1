$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

$paths = @{
    L3 = Join-Path $root 'src/native/src/L3CliWindow.cpp'
    Codex = Join-Path $root 'src/native/src/CodexRuntime.cpp'
    Harness = Join-Path $root 'src/native/src/HarnessProcessManager.cpp'
    CMake = Join-Path $root 'src/native/CMakeLists.txt'
    Product = Join-Path $root 'docs/TURINGDESK-PRODUCT-BASELINE.md'
    Native = Join-Path $root 'docs/TURINGDESK-NATIVE-TECH-BASELINE.md'
    Contract = Join-Path $root 'docs/L3-CODEX-RUNTIME-CONTRACT.md'
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
$codex = Get-Content $paths.Codex -Raw
$harness = Get-Content $paths.Harness -Raw
$cmake = Get-Content $paths.CMake -Raw
$product = Get-Content $paths.Product -Raw
$native = Get-Content $paths.Native -Raw
$contract = Get-Content $paths.Contract -Raw
$arm = Get-Content $paths.Arm -Raw
$x64 = Get-Content $paths.X64 -Raw

# Forward-only L3 architecture: Codex first, Direct API fallback only.
foreach ($marker in @(
    '#include "turingdesk/CodexRuntime.h"',
    'ActiveRuntime::Codex',
    'gCodexRuntime',
    'state.codex->AskAsync',
    'StartDirectFallback',
    'state.agent->AskAsync',
    'route: primary codex start',
    'fallback: direct api start',
    'primary=Codex CLI -> Relay/API; fallback=Direct API'
)) {
    if (-not $l3.Contains($marker)) {
        throw "Codex-first L3 marker missing: $marker"
    }
}
if ($l3.Contains('L3 runtime: TuringDesk Direct Model SSE')) {
    throw 'Architecture regression: Direct Model SSE returned as the primary L3 runtime.'
}

# CodexRuntime and NativeTools must be part of the ordinary TuringDesk binary.
foreach ($marker in @('src/CodexRuntime.cpp', 'src/NativeTools.cpp')) {
    if (-not $cmake.Contains($marker)) {
        throw "TuringDesk build graph marker missing: $marker"
    }
}

# Provider-neutral Codex transport and diagnostics.
foreach ($marker in @(
    'OpenAiResponsesBase',
    'ChatCompletionsBase',
    'setup.relayRequired = true',
    'app-server --stdio',
    'thread/start',
    'turn/start',
    'RuntimeLogPath(L"codex-runtime.log")',
    'notification.starts_with(L"Reconnecting...")',
    'app-server: transient reconnect notification:',
    'relay=exited code='
)) {
    if (-not $codex.Contains($marker)) {
        throw "Codex runtime contract marker missing: $marker"
    }
}
foreach ($marker in @('providerId == L"deepseek"', 'providerId) == L"deepseek"')) {
    if ($codex.Contains($marker)) {
        throw "Codex transport must not be hard-wired to a provider brand: $marker"
    }
}

# Harness must stay local and must never open an external browser itself.
$requiredHarnessArgs = 'constexpr wchar_t kHarnessArgs[] = L"web --host 127.0.0.1 --port 3080 --no-open";'
if (-not $harness.Contains($requiredHarnessArgs)) {
    throw 'Harness launch arguments must be loopback-only and include --no-open.'
}

# Both release workflows must run the architecture guard before build.
foreach ($workflow in @($arm, $x64)) {
    if (-not $workflow.Contains('verify-l3-runtime-contract.ps1')) {
        throw 'Cloud build is missing the L3 runtime contract guard.'
    }
}

# Active docs must agree on Codex-first and Direct fallback.
foreach ($doc in @($product, $native, $contract)) {
    if (-not $doc.Contains('Codex CLI')) {
        throw 'Current architecture documentation is missing Codex CLI.'
    }
    if (-not $doc.Contains('Direct Model')) {
        throw 'Current architecture documentation is missing Direct Model fallback.'
    }
}

Write-Host 'L3 runtime contract OK: Codex CLI primary, Relay/API transport, Direct API fallback only, no legacy/self-modifying restore path.'
