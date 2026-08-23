$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$l3Path = Join-Path $root 'src/native/src/L3CliWindow.cpp'
$searchPath = Join-Path $root 'src/native/src/SearchWindow.cpp'
$mainPath = Join-Path $root 'src/native/src/main.cpp'
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
$obsoleteDesignSpecPath = Join-Path $root 'docs/TURINGDESK-DESIGN-SPEC.md'
$retiredRuntimeHeaderPath = Join-Path $root 'src/native/include/turingdesk/DirectToolRuntime.h'
$retiredRuntimeV1Path = Join-Path $root 'src/native/src/DirectAgentRuntime.cpp'
$retiredRuntimeV2Path = Join-Path $root 'src/native/src/DirectAgentRuntimeV2.cpp'
$retiredRuntimeStubPath = Join-Path $root 'src/native/src/DirectToolRuntimeDisabled.cpp'

$requiredFiles = @(
    $l3Path, $searchPath, $mainPath, $codexPath, $cmakePath, $armWorkflowPath, $x64WorkflowPath,
    $deployCmdPath, $deployPs1Path, $productBaselinePath, $nativeBaselinePath,
    $contractDocPath, $readmePath
)
foreach ($path in $requiredFiles) {
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "L3 runtime contract input missing: $path"
    }
}
foreach ($path in @(
    $obsoleteDesignSpecPath,
    $retiredRuntimeHeaderPath,
    $retiredRuntimeV1Path,
    $retiredRuntimeV2Path,
    $retiredRuntimeStubPath
)) {
    if (Test-Path $path) {
        throw "Superseded architecture artifact must stay removed: $path"
    }
}

$l3 = Get-Content $l3Path -Raw
$search = Get-Content $searchPath -Raw
$main = Get-Content $mainPath -Raw
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

# 1) Runtime source: Codex must be primary and Direct API must remain fallback only.
$requiredL3 = @(
    '#include "turingdesk/CodexRuntime.h"',
    'ActiveRuntime::Codex',
    'gCodexRuntime',
    'state.codex->AskAsync',
    'state.agent->AskAsync',
    'route: primary codex start',
    'fallback: direct api start',
    'l3-runtime.log',
    'codex-runtime.log'
)
foreach ($marker in $requiredL3) {
    if (-not $l3.Contains($marker)) {
        throw "L3 Codex-first contract marker missing: $marker"
    }
}

$forbiddenRuntimeMarkers = @(
    'DirectToolRuntime',
    'WantsNativeTools',
    'ActiveRuntime::DirectTools',
    'DirectAgentRuntime.cpp',
    'DirectAgentRuntimeV2.cpp',
    'DirectToolRuntimeDisabled.cpp'
)
foreach ($marker in $forbiddenRuntimeMarkers) {
    if ($l3.Contains($marker) -or $main.Contains($marker) -or $cmake.Contains($marker)) {
        throw "Retired L3 runtime marker returned: $marker"
    }
}

# 1b) L3 CLI must share the application message loop and must not steal focus back to Search.
foreach ($forbidden in @(
    'while (IsWindow(window))',
    'GetMessageW(&msg'
)) {
    if ($l3.Contains($forbidden)) { throw "L3 window must not own a nested message loop: $forbidden" }
}
foreach ($required in @(
    'auto* state = new CliState{};',
    'case WM_NCDESTROY:',
    'reinterpret_cast<LONG_PTR>(state)'
)) {
    if (-not $l3.Contains($required)) { throw "L3 nonblocking lifecycle marker missing: $required" }
}
$startL3Start = $search.IndexOf('void SearchWindow::StartL3')
$startL3End = $search.IndexOf('void SearchWindow::SetStatus', $startL3Start)
if ($startL3Start -lt 0 -or $startL3End -le $startL3Start) { throw 'SearchWindow::StartL3 contract block missing.' }
$startL3Body = $search.Substring($startL3Start, $startL3End - $startL3Start)
if ($startL3Body.Contains('ShowAndFocus();')) { throw 'Closing/opening L3 must not force Search focus.' }

# 2) Build graph: CodexRuntime and NativeTools must be compiled into TuringDesk.
foreach ($marker in @(
    'src/CodexRuntime.cpp',
    'src/NativeTools.cpp',
    'TuringDeskL3ContractCheck',
    'add_dependencies(TuringDesk TuringDeskL3ContractCheck)'
)) {
    if (-not $cmake.Contains($marker)) {
        throw "CMake L3 build contract marker missing: $marker"
    }
}

# 3) Codex transport and diagnostics must stay capability-based, not brand-based.
foreach ($marker in @(
    'OpenAiResponsesBase',
    'ChatCompletionsBase',
    'setup.relayRequired = true',
    'codex-runtime.log',
    'relay: readiness failed',
    'app-server: initialize',
    'thread/start',
    'turn/start',
    'session: failed'
)) {
    if (-not $codex.Contains($marker)) {
        throw "Codex runtime diagnostic/transport marker missing: $marker"
    }
}
foreach ($marker in @(
    'providerId == L"deepseek"',
    'providerId) == L"deepseek"'
)) {
    if ($codex.Contains($marker)) {
        throw "Codex routing must not be hard-wired to DeepSeek: $marker"
    }
}

# 4) Cloud build: both x64 and ARM64 must run the same guard before Configure/Build.
foreach ($workflow in @($armWorkflow, $x64Workflow)) {
    foreach ($marker in @(
        'Verify L3 Codex-first runtime contract',
        '.\scripts\verify-l3-runtime-contract.ps1'
    )) {
        if (-not $workflow.Contains($marker)) {
            throw "Cloud build is missing L3 contract guard: $marker"
        }
    }
}
if ($armWorkflow.Contains('Remove-Item build/package/Codex -Recurse')) {
    throw 'ARM64 artifact must not delete bundled Codex CLI after validation.'
}
if ($armWorkflow.Contains('Remove-Item build/package/CodexRelay -Recurse')) {
    throw 'ARM64 artifact must not delete bundled Codex Relay after validation.'
}
foreach ($marker in @(
    'Validate full Codex CLI runtime',
    'Real Codex Relay protocol bridge smoke test',
    'Real Codex app-server through Relay end-to-end smoke test',
    '.\build\package\Codex\codex.exe',
    '.\build\package\CodexRelay\codex-relay.exe'
)) {
    if (-not $armWorkflow.Contains($marker)) {
        throw "ARM64 Codex validation marker missing: $marker"
    }
}

# 5) Local deploy entry points must enforce the same contract and runtime checks.
foreach ($marker in @(
    'verify-l3-runtime-contract.ps1',
    'Codex\codex.exe',
    'CodexRelay\codex-relay.exe',
    'L3 default route: Codex CLI'
)) {
    if (-not $deployCmd.Contains($marker)) {
        throw "Local deploy CMD Codex-first marker missing: $marker"
    }
}
foreach ($marker in @(
    'Assert-L3RuntimeContract',
    'verify-l3-runtime-contract.ps1',
    'Assert-DeployedCodexRuntime',
    'Codex\codex.exe',
    'CodexRelay\codex-relay.exe',
    'ArtifactCodex',
    'ArtifactRelay',
    'L3 default route: Codex CLI -> Relay/API; Direct API fallback only'
)) {
    if (-not $deployPs1.Contains($marker)) {
        throw "Direct deploy PowerShell Codex-first marker missing: $marker"
    }
}

# 6) Active documentation must contain the stable ASCII architecture terms.
foreach ($marker in @(
    'Codex CLI',
    'Direct Model fallback',
    'Chat Completions',
    'Codex Relay',
    'Responses Provider'
)) {
    if (-not $productBaseline.Contains($marker)) {
        throw "Product baseline L3 marker missing: $marker"
    }
}
foreach ($marker in @(
    'Codex CLI `app-server --stdio`',
    'Codex Relay',
    'Direct Model',
    'Provider',
    'scripts/verify-l3-runtime-contract.ps1'
)) {
    if (-not $nativeBaseline.Contains($marker)) {
        throw "Native baseline L3 marker missing: $marker"
    }
}
foreach ($marker in @(
    'Codex CLI',
    'Direct Model',
    'Provider',
    'l3-runtime.log',
    'codex-runtime.log',
    'app-server --stdio'
)) {
    if (-not $contractDoc.Contains($marker)) {
        throw "L3 design contract marker missing: $marker"
    }
}
foreach ($marker in @(
    'Codex CLI `app-server --stdio`',
    'Direct Model Runtime',
    'Codex Relay',
    '--no-open',
    'L3-CODEX-RUNTIME-CONTRACT.md'
)) {
    if (-not $readme.Contains($marker)) {
        throw "README runtime contract marker missing: $marker"
    }
}

Write-Host 'L3 runtime contract OK: Codex CLI primary, Relay/API transport, Direct API fallback, provider-neutral routing, diagnostics, packaging and retired-runtime cleanup are enforced.'
