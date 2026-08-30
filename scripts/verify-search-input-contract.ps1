$ErrorActionPreference = 'Stop'

$source = 'src/native/src/ui/search/SearchWindow.cpp'
if (-not (Test-Path $source -PathType Leaf)) {
    throw "Search UI source missing: $source"
}

$text = Get-Content $source -Raw
foreach ($marker in @(
    'CreateWindowExW(',
    'WS_EX_TOOLWINDOW | WS_EX_LAYERED',
    'CreateDCRenderTarget',
    'D2D1_ALPHA_MODE_PREMULTIPLIED',
    'UpdateLayeredWindow',
    'ULW_ALPHA',
    '0, L"EDIT"',
    'inputProxyWorks',
    'SendMessageW(edit_, WM_CHAR',
    'case WM_LBUTTONDOWN:',
    'SetFocus(edit_)'
)) {
    if (-not $text.Contains($marker)) {
        throw "Search input/rendering contract marker missing: $marker"
    }
}

# Search remains a DirectWrite-rendered Search surface. Its native EDIT is input infrastructure
# only; InputImeAnchor owns the shared Windows/TSF plumbing.
$paintSuppressed = $text -match '(?s)if\s*\(message\s*==\s*WM_PAINT\)\s*\{\s*ValidateRect\(hwnd,\s*nullptr\);\s*return\s+0;\s*\}'
if (-not $paintSuppressed) {
    throw 'Search input contract missing native EDIT WM_PAINT suppression.'
}

foreach ($marker in @(
    'CreateWindowExW(WS_EX_LAYERED, L"EDIT"',
    'SetLayeredWindowAttributes(edit_',
    'CreateRoundRectRgn',
    'SetWindowRgn',
    'ApplyWindowShape',
    'ID2D1HwndRenderTarget'
)) {
    if ($text.Contains($marker)) {
        throw "Forbidden Search input/rendering implementation returned: $marker"
    }
}

$header = 'src/native/include/miaodesk/SearchWindow.h'
if (-not (Test-Path $header -PathType Leaf)) { throw "Search header missing: $header" }
$headerText = Get-Content $header -Raw
if (-not $headerText.Contains('#include "miaodesk/InputImeAnchor.h"')) {
    throw 'SearchWindow must consume the shared InputImeAnchor framework.'
}
if (-not $headerText.Contains('class SearchWindow')) {
    throw 'Search business/surface naming must remain SearchWindow.'
}

# Run the generic infrastructure/profile contract as part of Search validation, but do not rename
# Search-specific concepts to Input: Input is the framework, Search is the surface profile.
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File '.\scripts\verify-input-ime-contract.ps1'
if ($LASTEXITCODE -ne 0) {
    throw "Shared Input IME contract failed: $LASTEXITCODE"
}

Write-Host 'Search input contract OK: Search surface preserved and backed by shared InputImeAnchor infrastructure.'
