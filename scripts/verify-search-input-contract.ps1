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

# The native EDIT remains visually suppressed so it can provide keyboard/IME/clipboard input
# without creating a second visible rectangle. Its geometry/caret are maintained by the IME bridge.
$paintSuppressed = $text -match '(?s)if\s*\(message\s*==\s*WM_PAINT\)\s*\{\s*ValidateRect\(hwnd,\s*nullptr\);\s*return\s+0;\s*\}'
if (-not $paintSuppressed) {
    throw 'Search input contract missing native EDIT WM_PAINT suppression.'
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
    'EnsureImeProxyGeometry',
    'kImeProxyHeight',
    'kVisibleEditRight - kVisibleEditLeft',
    'CFS_FORCE_POSITION',
    'CFS_EXCLUDE',
    'HideCaret(edit)',
    'SetCaretPos(caretX, 6)',
    'WH_CALLWNDPROCRET',
    'SearchImeCallWndRetProc',
    'IMN_OPENCANDIDATE',
    'IMN_CHANGECANDIDATE',
    'ImmSetCompositionWindow',
    'ImmSetCandidateWindow'
)

foreach ($marker in $imeRequired) {
    if (-not $imeText.Contains($marker)) {
        throw "Search IME TSF/non-reentrant contract marker missing: $marker"
    }
}

# IMM32 setters can synchronously emit IME notifications. WM_IME_NOTIFY may request a deferred
# refresh after candidate creation, but it must never invoke the setters/anchor synchronously.
if ($imeText -match '(?s)case\s+WM_IME_NOTIFY\s*:.*?AnchorImeToVisibleCaret') {
    throw 'Search IME bridge must not synchronously anchor from WM_IME_NOTIFY.'
}
if ($imeText -notmatch '(?s)case\s+WM_IME_NOTIFY\s*:.*?IMN_OPENCANDIDATE.*?RequestImeAnchor') {
    throw 'Search IME bridge must defer candidate anchoring when the IME opens its candidate UI.'
}

# Focus must fix the EDIT rectangle before TSF processes the focus transaction, while IMM32
# setters themselves remain deferred to the custom message.
if ($imeText -notmatch '(?s)case\s+WM_SETFOCUS\s*:.*?EnsureImeProxyGeometry.*?RequestImeAnchor') {
    throw 'Search IME bridge must synchronously restore geometry and defer IMM anchoring on focus.'
}
if ($imeText -match '(?s)case\s+WM_SETFOCUS\s*:.*?AnchorImeToVisibleCaret') {
    throw 'Search IME bridge must defer WM_SETFOCUS anchoring instead of anchoring synchronously.'
}

# SearchWindow currently resizes its infrastructure EDIT during WM_SIZE. A post-dispatch hook is
# required so the 1x1 state cannot leak into the next TSF/candidate message.
if ($imeText -notmatch '(?s)SearchImeCallWndRetProc.*?WM_SIZE.*?EnsureImeProxyGeometry') {
    throw 'Search IME bridge must restore input geometry after SearchWindow WM_SIZE returns.'
}

# The focused EDIT must expose a real rectangle matching the custom DirectWrite field before
# composition/candidate positioning is attempted.
if ($imeText -notmatch '(?s)SetWindowPos\s*\(\s*edit.*?kVisibleEditLeft.*?kImeProxyTop.*?kVisibleEditRight\s*-\s*kVisibleEditLeft.*?kImeProxyHeight') {
    throw 'Search IME bridge must restore the native input proxy to visible search-field geometry.'
}

Write-Host 'Search input proxy, stable TSF caret/geometry, deferred Chinese IME anchoring, and per-pixel-alpha rendering contracts verified.'
