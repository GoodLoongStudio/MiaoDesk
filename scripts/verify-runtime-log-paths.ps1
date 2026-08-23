$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $root 'src/native/src'
$headerPath = Join-Path $root 'src/native/include/turingdesk/RuntimeLogPaths.h'

if (-not (Test-Path $headerPath -PathType Leaf)) {
    throw "Runtime log path helper missing: $headerPath"
}

$header = Get-Content $headerPath -Raw
foreach ($marker in @('FOLDERID_Desktop', 'TuringDesk-Logs', 'RuntimeLogPath')) {
    if (-not $header.Contains($marker)) {
        throw "Desktop runtime log helper marker missing: $marker"
    }
}

$logLiteralPattern = 'L"([^"\r\n]+\.log)"'
$files = Get-ChildItem $sourceRoot -Filter '*.cpp' -File -Recurse
foreach ($file in $files) {
    $text = Get-Content $file.FullName -Raw
    $matches = [regex]::Matches($text, $logLiteralPattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    foreach ($match in $matches) {
        $name = $match.Groups[1].Value
        $expected = 'RuntimeLogPath(L"' + $name + '")'
        if (-not $text.Contains($expected)) {
            throw "Runtime log must use Desktop RuntimeLogPath helper: $($file.FullName) -> $name"
        }
    }
}

Write-Host 'Runtime log contract OK: all native .log files use Desktop/TuringDesk-Logs.'
