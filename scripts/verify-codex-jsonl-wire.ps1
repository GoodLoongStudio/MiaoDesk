$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$headerPath = Join-Path $root 'src/native/include/turingdesk/NativeTools.h'
$nativeToolsPath = Join-Path $root 'src/native/src/NativeTools.cpp'
$codexPath = Join-Path $root 'src/native/src/CodexRuntime.cpp'
$cmakePath = Join-Path $root 'src/native/CMakeLists.txt'

foreach ($path in @($headerPath, $nativeToolsPath, $codexPath, $cmakePath)) {
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "Codex JSONL contract input missing: $path"
    }
}

$header = Get-Content $headerPath -Raw
$nativeTools = Get-Content $nativeToolsPath -Raw
$codex = Get-Content $codexPath -Raw
$cmake = Get-Content $cmakePath -Raw

foreach ($marker in @(
    'NativeToolDefinitionsJsonRaw',
    'TURINGDESK_NATIVE_TOOLS_IMPL',
    "json.erase(std::remove(json.begin(), json.end(), '\r')",
    "json.erase(std::remove(json.begin(), json.end(), '\n')",
    'return json;'
)) {
    if (-not $header.Contains($marker)) {
        throw "Native tool JSONL compaction marker missing: $marker"
    }
}

if (-not $nativeTools.Contains('std::string NativeToolDefinitionsJson()')) {
    throw 'NativeTools.cpp raw registry definition missing.'
}

foreach ($marker in @(
    'NativeToolDefinitionsJson()',
    '"dynamicTools"',
    'WriteLine(threadStart)'
)) {
    if (-not $codex.Contains($marker)) {
        throw "Codex thread/start wire marker missing: $marker"
    }
}

foreach ($marker in @(
    'src/NativeTools.cpp',
    'TURINGDESK_NATIVE_TOOLS_IMPL',
    'verify-codex-jsonl-wire.ps1',
    'TuringDeskCodexJsonlContractCheck',
    'add_dependencies(TuringDesk TuringDeskCodexJsonlContractCheck)'
)) {
    if (-not $cmake.Contains($marker)) {
        throw "CMake Codex JSONL guard marker missing: $marker"
    }
}

Write-Host 'Codex JSONL contract OK: dynamicTools is compacted to one physical line before thread/start.'
