$ErrorActionPreference = 'Stop'

$source = 'src/native/src/ui/search/SearchWindow.cpp'
if (-not (Test-Path $source -PathType Leaf)) {
    throw "Search UI source missing: $source"
}

$text = Get-Content $source -Raw

$required = @(
    'CreateWindowExW(0, L"EDIT"',
    'kInputProxyY, 1, 1',
    'inputProxyWorks',
    'SendMessageW(edit_, WM_CHAR',
    'case WM_LBUTTONDOWN:',
    'SetFocus(edit_)',
    'if (message == WM_PAINT) { ValidateRect(hwnd, nullptr); return 0; }'
)

foreach ($marker in $required) {
    if (-not $text.Contains($marker)) {
        throw "Search input contract marker missing: $marker"
    }
}

$forbidden = @(
    'CreateWindowExW(WS_EX_LAYERED, L"EDIT"',
    'SetLayeredWindowAttributes(edit_',
    'LWA_ALPHA'
)

foreach ($marker in $forbidden) {
    if ($text.Contains($marker)) {
        throw "Forbidden Search input implementation returned: $marker"
    }
}

Write-Host 'Search input proxy contract verified.'
