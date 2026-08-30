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

$imeSource = 'src/native/include/miaodesk/SearchImeAnchor.h'
if (-not (Test-Path $imeSource -PathType Leaf)) {
    throw "Search IME bridge missing: $imeSource"
}

$imeText = Get-Content $imeSource -Raw
$imeRequired = @(
    'kDeferredImeAnchorMessage',
    'gImeAnchorBusy',
    'gImeAnchorPending',
    'GetFocus() != edit',
    'PostMessageW(edit, kDeferredImeAnchorMessage',
    'message->message == kDeferredImeAnchorMessage',
    'ImmSetCompositionWindow',
    'ImmSetCandidateWindow'
)

foreach ($marker in $imeRequired) {
    if (-not $imeText.Contains($marker)) {
        throw "Search IME non-reentrant contract marker missing: $marker"
    }
}

# IMM32 setters can synchronously emit IME notifications. Re-entering the anchor from
# WM_IME_NOTIFY is a focus-click freeze risk, so notification handling must stay one-way.
if ($imeText -match '(?s)case\s+WM_IME_NOTIFY\s*:.*?(AnchorImeToVisibleCaret|RequestImeAnchor)') {
    throw 'Search IME bridge must not feed WM_IME_NOTIFY back into candidate anchoring.'
}

# Focus/IME hooks schedule anchoring after the triggering message instead of calling IMM32
# from inside the WH_CALLWNDPROC callback. This keeps click-to-focus and text entry responsive.
if ($imeText -match '(?s)case\s+WM_SETFOCUS\s*:.*?AnchorImeToVisibleCaret') {
    throw 'Search IME bridge must defer WM_SETFOCUS anchoring instead of anchoring synchronously.'
}

Write-Host 'Search input proxy, IME reentrancy, and per-pixel-alpha rendering contracts verified.'
