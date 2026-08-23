$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

$requiredDocs = @(
    'docs/TURINGDESK-DESIGN-SPEC.md',
    'docs/V1-SCENE-RELEASE-SCOPE.md',
    'docs/AI-WORKBENCH-CONSOLIDATION-PLAN.md',
    'docs/LEGACY-REDUNDANCY-CLEANUP-PLAN.md'
)
foreach ($relative in $requiredDocs) {
    $path = Join-Path $root $relative
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "Required release baseline missing: $relative"
    }
}

$l3Path = Join-Path $root 'src/native/src/L3CliWindow.cpp'
$searchPath = Join-Path $root 'src/native/src/SearchWindow.cpp'
$cmakePath = Join-Path $root 'src/native/CMakeLists.txt'
$harnessPath = Join-Path $root 'src/native/src/HarnessProcessManager.cpp'
$armWorkflowPath = Join-Path $root '.github/workflows/native-search-windows.yml'
$x64WorkflowPath = Join-Path $root '.github/workflows/native-x64-validation.yml'

foreach ($path in @($l3Path, $searchPath, $cmakePath, $harnessPath, $armWorkflowPath, $x64WorkflowPath)) {
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "Contract input missing: $path"
    }
}

$l3 = Get-Content $l3Path -Raw
$search = Get-Content $searchPath -Raw
$cmake = Get-Content $cmakePath -Raw
$harness = Get-Content $harnessPath -Raw
$armWorkflow = Get-Content $armWorkflowPath -Raw
$x64Workflow = Get-Content $x64WorkflowPath -Raw

# Ordinary L3 must be owned by TuringDesk: local in-process tools first, Direct Model SSE for chat.
foreach ($marker in @(
    'state.agent->TryHandleLocal',
    'state.agent->AskAsync',
    'kDeltaMessage',
    'kDoneMessage',
    'case WM_NCDESTROY:',
    'RuntimeLogPath(L"l3-runtime.log")'
)) {
    if (-not $l3.Contains($marker)) {
        throw "L3 lightweight contract marker missing: $marker"
    }
}

foreach ($marker in @(
    'CodexRuntime',
    'gCodexRuntime',
    'ActiveRuntime::Codex',
    'kCodexDoneMessage',
    '4317',
    '4318',
    'MCP'
)) {
    if ($l3.Contains($marker)) {
        throw "External or legacy runtime marker returned to ordinary L3: $marker"
    }
}

if ($cmake.Contains('src/CodexRuntime.cpp')) {
    throw 'Ordinary TuringDesk binary must not compile CodexRuntime.cpp.'
}
foreach ($source in @('src/AppSearch.cpp', 'src/GozSearch.cpp')) {
    $count = ([regex]::Matches($cmake, [regex]::Escape($source))).Count
    if ($count -ne 1) {
        throw "Ordinary TuringDesk binary must compile $source exactly once; found $count entries."
    }
}

# L3 window must share the application message loop and must not steal focus back to Search.
foreach ($marker in @('while (IsWindow(window))', 'GetMessageW(&msg')) {
    if ($l3.Contains($marker)) {
        throw "L3 window owns a nested message loop: $marker"
    }
}
foreach ($marker in @('auto* state = new CliState{};', 'case WM_NCDESTROY:', 'reinterpret_cast<LONG_PTR>(state)')) {
    if (-not $l3.Contains($marker)) {
        throw "L3 nonblocking lifecycle marker missing: $marker"
    }
}

$start = $search.IndexOf('void SearchWindow::StartL3')
if ($start -lt 0) {
    throw 'SearchWindow::StartL3 missing.'
}
$length = [Math]::Min(1800, $search.Length - $start)
$startL3Body = $search.Substring($start, $length)
if ($startL3Body.Contains('ShowAndFocus();')) {
    throw 'L3 must not force Search focus after opening or closing.'
}

# L4 Harness must remain loopback-only. Self-test strings may mention forbidden alternatives,
# so assert the actual launch constant instead of banning those words globally.
$requiredHarnessArgs = 'constexpr wchar_t kHarnessArgs[] = L"web --host 127.0.0.1 --port 3080 --no-open";'
if (-not $harness.Contains($requiredHarnessArgs)) {
    throw 'Harness launch arguments must be loopback-only and include --no-open.'
}
foreach ($marker in @('4317', '4318', 'MCP')) {
    if ($harness.Contains($marker)) {
        throw "Harness legacy marker present: $marker"
    }
}

# Both release architectures must execute the same guard before build.
foreach ($workflow in @($armWorkflow, $x64Workflow)) {
    if (-not $workflow.Contains('verify-l3-runtime-contract.ps1')) {
        throw 'Cloud build is missing the L3 runtime contract guard.'
    }
}

Write-Host 'L3 contract OK: in-process local tools, Direct Model SSE, explicit L4 Harness, loopback-only workbench.'
