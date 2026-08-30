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
# without creating a second visible rectangle. Its geometry/caret are maintained by InputImeAnchor.
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

$imeSource = 'src/native/include/miaodesk/InputImeAnchor.h'
if (-not (Test-Path $imeSource -PathType Leaf)) {
    throw "Shared input IME bridge missing: $imeSource"
}
if (Test-Path 'src/native/include/miaodesk/SearchImeAnchor.h' -PathType Leaf) {
    throw 'Legacy SearchImeAnchor.h returned; shared framework must live in InputImeAnchor.h.'
}

$imeText = Get-Content $imeSource -Raw
$imeRequired = @(
    'namespace miaodesk::input_ime_detail',
    'kDeferredSearchImeAnchorMessage',
    'gSearchImeAnchorBusy',
    'gSearchImeAnchorPending',
    'GetFocus() != edit',
    'PostMessageW(edit, kDeferredSearchImeAnchorMessage',
    'message.message == kDeferredSearchImeAnchorMessage',
    'EnsureSearchImeGeometry',
    'kSearchEditHeight',
    'kSearchEditRight - kSearchEditLeft',
    'CFS_FORCE_POSITION',
    'CFS_EXCLUDE',
    'HideCaret(edit)',
    'SetCaretPos(caretX, 6)',
    'WH_CALLWNDPROCRET',
    'InputImeCallWndRetProc',
    'InputImeAnchorBridge',
    'IMN_OPENCANDIDATE',
    'IMN_CHANGECANDIDATE',
    'ImmSetCompositionWindow',
    'ImmSetCandidateWindow',
    'EnsureConversationImeGeometry',
    'SyncConversationImeAnchor'
)

foreach ($marker in $imeRequired) {
    if (-not $imeText.Contains($marker)) {
        throw "Input IME TSF/non-reentrant contract marker missing: $marker"
    }
}

# Framework naming is Input; surface profiles remain Search and Conversation.
if ($imeText.Contains('namespace miaodesk::search_ime_detail') -or
    $imeText.Contains('class SearchImeAnchorBridge')) {
    throw 'Shared IME framework still carries Search-only naming.'
}
if (-not $imeText.Contains('HandleSearchEditMessageBefore') -or
    -not $imeText.Contains('HandleConversationEditMessageBefore')) {
    throw 'InputImeAnchor must retain explicit Search and Conversation surface profiles.'
}

# IMM32 setters can synchronously emit IME notifications. Candidate notifications may request
# a deferred refresh, but must never invoke the Search anchor synchronously.
if ($imeText -match '(?s)case\s+WM_IME_NOTIFY\s*:.*?AnchorSearchImeToVisibleCaret') {
    throw 'Search IME profile must not synchronously anchor from WM_IME_NOTIFY.'
}
if ($imeText -notmatch '(?s)case\s+WM_IME_NOTIFY\s*:.*?IMN_OPENCANDIDATE.*?RequestSearchImeAnchor') {
    throw 'Search IME profile must defer candidate anchoring when candidate UI opens.'
}

# Focus must fix the Search EDIT rectangle before TSF processes focus, while IMM setters remain deferred.
if ($imeText -notmatch '(?s)HandleSearchEditMessageBefore.*?case\s+WM_SETFOCUS\s*:.*?EnsureSearchImeGeometry.*?RequestSearchImeAnchor') {
    throw 'Search IME profile must synchronously restore geometry and defer IMM anchoring on focus.'
}
if ($imeText -match '(?s)HandleSearchEditMessageBefore.*?case\s+WM_SETFOCUS\s*:.*?AnchorSearchImeToVisibleCaret') {
    throw 'Search IME profile must defer WM_SETFOCUS anchoring instead of anchoring synchronously.'
}

# SearchWindow still resizes its infrastructure EDIT during WM_SIZE. The generic post-dispatch
# hook must restore Search geometry before the next TSF/candidate message.
if ($imeText -notmatch '(?s)InputImeCallWndRetProc.*?WM_SIZE.*?IsMiaoDeskSearchWindow.*?EnsureSearchImeGeometry') {
    throw 'Input IME bridge must restore Search input geometry after SearchWindow WM_SIZE returns.'
}

# The focused Search EDIT must expose the real DirectWrite text rectangle.
if ($imeText -notmatch '(?s)SetWindowPos\s*\(\s*edit.*?kSearchEditLeft.*?kSearchEditTop.*?kSearchEditRight\s*-\s*kSearchEditLeft.*?kSearchEditHeight') {
    throw 'Search IME profile must restore the native EDIT to the visible search-field geometry.'
}

# Conversation must no longer own an independent 1x1/CFS_POINT positioning implementation.
$conversationOverlay = 'src/native/src/ui/ai/ConversationPanelInputOverlay.inc'
$conversationSource = 'src/native/src/ui/ai/ConversationPanel.cpp'
foreach ($path in @($conversationOverlay, $conversationSource)) {
    if (-not (Test-Path $path -PathType Leaf)) { throw "Conversation input source missing: $path" }
}
$conversationOverlayText = Get-Content $conversationOverlay -Raw
$conversationText = Get-Content $conversationSource -Raw

$conversationForbidden = @(
    'MoveWindow(state.input, caret.x, caret.y, 1, 1',
    'composition.dwStyle = CFS_POINT',
    'candidate.dwStyle = CFS_CANDIDATEPOS'
)
foreach ($marker in $conversationForbidden) {
    if ($conversationOverlayText.Contains($marker)) {
        throw "Legacy Conversation IME implementation returned: $marker"
    }
}
if (-not $conversationOverlayText.Contains('input_ime_detail::SyncConversationImeAnchor(state.input)')) {
    throw 'Conversation overlay must delegate native IME geometry to InputImeAnchor.'
}
if (-not $conversationText.Contains('input_ime_detail::HasImeComposition(state->input)') -or
    -not $conversationText.Contains('static_cast<UINT32>(inputText.size())')) {
    throw 'Conversation visual caret must follow the visible IME composition end.'
}

Write-Host 'Shared InputImeAnchor framework, Search profile, Conversation IME delegation, composition caret, and per-pixel-alpha contracts verified.'
