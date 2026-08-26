$ErrorActionPreference = 'Stop'

$source = 'src/native/src/ui/search/SearchWindow.cpp'
if (-not (Test-Path $source -PathType Leaf)) {
    throw "Search UI source missing: $source"
}

$text = Get-Content $source -Raw

$required = @(
    'CreateWindowExW(',
    'WS_EX_TOOLWINDOW | WS_EX_LAYERED',
    'CreateDCRenderTarget',
    'D2D1_ALPHA_MODE_PREMULTIPLIED',
    'UpdateLayeredWindow',
    'ULW_ALPHA',
    '0, L"EDIT"',
    'kInputProxyY, 1, 1',
    'inputProxyWorks',
    'SendMessageW(edit_, WM_CHAR',
    'case WM_LBUTTONDOWN:',
    'SetFocus(edit_)'
)

foreach ($marker in $required) {
    if (-not $text.Contains($marker)) {
        throw "Search input/rendering contract marker missing: $marker"
    }
}

# The hidden native EDIT must suppress its own painting so it can provide IME/keyboard/clipboard
# input without ever becoming a visible rectangle. Match semantics rather than one formatting style.
$paintSuppressed = $text -match '(?s)if\s*\(message\s*==\s*WM_PAINT\)\s*\{\s*ValidateRect\(hwnd,\s*nullptr\);\s*return\s+0;\s*\}'
if (-not $paintSuppressed) {
    throw 'Search input contract missing hidden EDIT WM_PAINT suppression.'
}

$forbidden = @(
    'CreateWindowExW(WS_EX_LAYERED, L"EDIT"',
    'SetLayeredWindowAttributes(edit_',
    'CreateRoundRectRgn',
    'SetWindowRgn',
    'ApplyWindowShape',
    'ID2D1HwndRenderTarget'
)

foreach ($marker in $forbidden) {
    if ($text.Contains($marker)) {
        throw "Forbidden Search input/rendering implementation returned: $marker"
    }
}

Write-Host 'Search input proxy and per-pixel-alpha rendering contract verified.'
