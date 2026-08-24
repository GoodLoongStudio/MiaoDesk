$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

$paths = @{
    L3 = Join-Path $root 'src/native/src/L3CliWindow.cpp'
    Pi = Join-Path $root 'src/native/src/PiRuntime.cpp'
    Harness = Join-Path $root 'src/native/src/HarnessProcessManager.cpp'
    CMake = Join-Path $root 'src/native/CMakeLists.txt'
    Product = Join-Path $root 'docs/TURINGDESK-PRODUCT-BASELINE.md'
    Native = Join-Path $root 'docs/TURINGDESK-NATIVE-TECH-BASELINE.md'
    Contract = Join-Path $root 'docs/L3-PI-RUNTIME-CONTRACT.md'
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
$harness = Get-Content $paths.Harness -Raw
$cmake = Get-Content $paths.CMake -Raw
$product = Get-Content $paths.Product -Raw
$native = Get-Content $paths.Native -Raw
$contract = Get-Content $paths.Contract -Raw
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
    if (-not $l3.Contains($marker)) {
        throw "Pi-first L3 marker missing: $marker"
    }
}
if ($l3.Contains('ActiveRuntime::Codex') -or $l3.Contains('CodexRuntime')) {
    throw 'Architecture regression: retired Codex runtime returned to L3 UI.'
}

# PiRuntime and TuringDesk product tools must be part of the ordinary binary.
foreach ($marker in @('src/PiRuntime.cpp', 'src/NativeTools.cpp')) {
    if (-not $cmake.Contains($marker)) {
        throw "TuringDesk build graph marker missing: $marker"
    }
}
foreach ($marker in @('src/CodexRuntime.cpp', 'src/CodexHostBridge.cpp', 'TuringDeskCodexJsonlContractCheck')) {
    if ($cmake.Contains($marker)) {
        throw "Retired Codex build marker is still active: $marker"
    }
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
    '"type":"prompt"',
    '"type":"abort"',
    '"type":"new_session"'
)) {
    if (-not $pi.Contains($marker)) {
        throw "Pi runtime contract marker missing: $marker"
    }
}
foreach ($marker in @('providerId == L"deepseek"', 'providerId) == L"deepseek"')) {
    if ($pi.Contains($marker)) {
        throw "Pi transport must not be hard-wired to a provider brand: $marker"
    }
}

# Harness stays local and never opens an external browser itself.
$requiredHarnessArgs = 'constexpr wchar_t kHarnessArgs[] = L"web --host 127.0.0.1 --port 3080 --no-open";'
if (-not $harness.Contains($requiredHarnessArgs)) {
    throw 'Harness launch arguments must be loopback-only and include --no-open.'
}

# Both release workflows must run the same architecture guard before build.
foreach ($workflow in @($arm, $x64)) {
    if (-not $workflow.Contains('verify-l3-runtime-contract.ps1')) {
        throw 'Cloud build is missing the L3 runtime contract guard.'
    }
}

# Active architecture docs must agree on Pi-first and Direct fallback.
foreach ($doc in @($product, $native, $contract)) {
    if (-not $doc.Contains('Pi')) {
        throw 'Current architecture documentation is missing Pi runtime.'
    }
    if (-not $doc.Contains('Direct Model')) {
        throw 'Current architecture documentation is missing Direct Model fallback.'
    }
    if ($doc.Contains('Codex CLI')) {
        throw 'Current architecture documentation still contains the retired Codex CLI design.'
    }
}

Write-Host 'L3 runtime contract OK: Pi Agent primary, provider-neutral API routing, Direct API fallback only.'
