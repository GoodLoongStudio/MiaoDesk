$ErrorActionPreference = 'Stop'

$imeSource = 'src/native/include/miaodesk/InputImeAnchor.h'
$sessionSource = 'src/native/include/miaodesk/InputImeSession.h'
$searchHeader = 'src/native/include/miaodesk/SearchWindow.h'
$conversationOverlay = 'src/native/src/ui/ai/ConversationPanelInputOverlay.inc'
$conversationSource = 'src/native/src/ui/ai/ConversationPanel.cpp'
$legacyImeSource = 'src/native/include/miaodesk/SearchImeAnchor.h'

foreach ($path in @($imeSource, $sessionSource, $searchHeader, $conversationOverlay, $conversationSource)) {
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "Input IME contract source missing: $path"
    }
}
if (Test-Path $legacyImeSource -PathType Leaf) {
    throw 'SearchImeAnchor.h must not return; the shared framework is InputImeAnchor.h.'
}

$ime = Get-Content $imeSource -Raw
$session = Get-Content $sessionSource -Raw
$search = Get-Content $searchHeader -Raw
$overlay = Get-Content $conversationOverlay -Raw
$conversation = Get-Content $conversationSource -Raw

# Framework is Input. Search and Conversation remain explicit surface profiles.
foreach ($marker in @(
    'namespace miaodesk::input_ime_detail',
    'class InputImeAnchorBridge',
    'InputImeCallWndProc',
    'InputImeCallWndRetProc',
    'PublishInputImeAnchor',
    'HandleSearchEditMessageBefore',
    'HandleSearchEditMessageAfter',
    'HandleConversationEditMessageBefore',
    'HandleConversationEditMessageAfter',
    'EnsureSearchImeGeometry',
    'AnchorSearchImeToVisibleCaret',
    'RequestSearchImeAnchor',
    'EnsureConversationImeGeometry',
    'AnchorConversationImeToNativeCaret',
    'RequestConversationImeAnchor',
    'SyncConversationImeAnchor'
)) {
    if (-not $ime.Contains($marker)) {
        throw "Input IME framework/profile marker missing: $marker"
    }
}

foreach ($forbidden in @(
    'namespace miaodesk::search_ime_detail',
    'class SearchImeAnchorBridge',
    'gSearchImeAnchorBridge'
)) {
    if ($ime.Contains($forbidden)) {
        throw "Shared Input IME framework still has Search-only framework naming: $forbidden"
    }
}

# InputImeSession owns the common IME lifecycle/result/key contract. Search additionally projects
# provisional composition into its existing query model; Conversation keeps its existing Direct2D
# composition visual above the session subclass and must not receive a second GETTEXT projection.
foreach ($marker in @(
    'namespace miaodesk::input_ime_session_detail',
    'struct InputImeSessionState',
    'class InputImeSessionBridge',
    'InputImeSessionProc',
    'WM_IME_STARTCOMPOSITION',
    'WM_IME_COMPOSITION',
    'WM_IME_ENDCOMPOSITION',
    'GCS_COMPSTR',
    'GCS_RESULTSTR',
    'EM_REPLACESEL',
    'VirtualizeSearchGetText',
    'VirtualizeSearchSelection',
    'NotifySearchVisibleQuery',
    'NotifyConversationInputVisual',
    'IsImeOwnedNavigationKey',
    'HasActiveComposition'
)) {
    if (-not $session.Contains($marker)) {
        throw "Input IME session marker missing: $marker"
    }
}
if (-not $search.Contains('#include "miaodesk/InputImeSession.h"')) {
    throw 'SearchWindow must consume the shared InputImeSession framework.'
}

# Custom-rendered inputs own composition visuals. Stock EDIT must not receive the three
# composition messages, otherwise Microsoft Pinyin paints a second inline composition rectangle.
foreach ($pattern in @(
    '(?s)message == WM_IME_STARTCOMPOSITION.*?BeginComposition\(hwnd, core\).*?return 0;',
    '(?s)message == WM_IME_COMPOSITION.*?UpdateComposition\(hwnd, lParam, core\).*?return 0;',
    '(?s)message == WM_IME_ENDCOMPOSITION.*?EndComposition\(hwnd\).*?return 0;'
)) {
    if ($session -notmatch $pattern) {
        throw "InputImeSession must own the stock EDIT composition path: $pattern"
    }
}

# Search must treat live composition as visible query text/selection so the placeholder vanishes,
# the result panel updates before candidate commit, and the visual/system caret sits at comp end.
foreach ($marker in @(
    'MAKEWPARAM(input_ime_detail::kSearchEditControlId, EN_CHANGE)',
    'visible += state->composition',
    'state->replaceStart +',
    'static_cast<DWORD>(state->composition.size())',
    'if (search && (message == WM_GETTEXT || message == WM_GETTEXTLENGTH))'
)) {
    if (-not $session.Contains($marker)) {
        throw "Search live-composition contract marker missing: $marker"
    }
}

# Conversation already appends its Direct2D GCS_COMPSTR in ConversationPanelInputOverlay. The
# shared session must not virtualize WM_GETTEXT for Conversation too, which would duplicate pinyin.
if ($session -match '(?s)\(search\s*\|\|\s*conversation\).*?WM_GETTEXT') {
    throw 'Conversation composition is being virtualized twice; only Search may project WM_GETTEXT.'
}
if (-not $overlay.Contains('gConversationImeComposition')) {
    throw 'Conversation Direct2D composition visual owner unexpectedly disappeared without a replacement migration.'
}

# Search/Conversation product shortcuts must not steal Enter/Esc/arrows from an active IME.
if ($session -notmatch '(?s)message == WM_KEYDOWN.*?HasActiveComposition\(hwnd\).*?IsImeOwnedNavigationKey\(wParam\).*?return 0;') {
    throw 'Active IME navigation/commit keys are not protected from product-level shortcuts.'
}

# A full-size native EDIT can receive the mouse directly instead of the parent overlay. The shared
# session must explicitly focus it and then refresh the appropriate product surface.
foreach ($marker in @(
    'message == WM_LBUTTONDOWN',
    'SetFocus(hwnd)',
    'NotifyConversationInputVisual(hwnd)'
)) {
    if (-not $session.Contains($marker)) {
        throw "Conversation input focus contract marker missing: $marker"
    }
}

# The common publication path is the only owner of Windows caret + IMM32 positioning.
foreach ($marker in @(
    'SetCaretPos(caret.x, caret.y)',
    'CFS_FORCE_POSITION',
    'CFS_EXCLUDE',
    'ImmSetCompositionWindow',
    'ImmSetCandidateWindow',
    'HideCaret(edit)'
)) {
    if (-not $ime.Contains($marker)) {
        throw "Shared IME publication marker missing: $marker"
    }
}

# Search keeps the already validated Search geometry and deferred candidate behavior.
foreach ($marker in @(
    'kSearchEditLeft = 52',
    'kSearchEditRight = 594',
    'kSearchEditTop = 13',
    'kSearchEditHeight = 30',
    'kDeferredSearchImeAnchorMessage',
    'IMN_OPENCANDIDATE',
    'IMN_CHANGECANDIDATE'
)) {
    if (-not $ime.Contains($marker)) {
        throw "Search IME profile marker missing: $marker"
    }
}

# Conversation must expose the same real text rectangle that DirectWrite uses and must move the
# published system/TSF caret to the end of the custom-rendered GCS_COMPSTR composition.
foreach ($marker in @(
    'visibleRight = static_cast<int>(client.right) - ConversationPx(parent, 162)',
    'ReadImeCompositionText(edit)',
    'MeasureEditTextWidth(',
    'caret.x += MeasureEditTextWidth',
    'input_ime_detail::SyncConversationImeAnchor(state.input)',
    'input_ime_detail::HasImeComposition(state->input)'
)) {
    if (-not ($ime + $overlay + $conversation).Contains($marker)) {
        throw "Conversation shared-input marker missing: $marker"
    }
}

# Active Conversation input code may not recreate the retired 1x1/point-anchor architecture.
foreach ($forbidden in @(
    'MoveWindow(state.input, caret.x, caret.y, 1, 1',
    'composition.dwStyle = CFS_POINT',
    'candidate.dwStyle = CFS_CANDIDATEPOS'
)) {
    if ($overlay.Contains($forbidden)) {
        throw "Retired Conversation IME path returned: $forbidden"
    }
}

Write-Host 'Input IME contract OK: shared anchor/lifecycle; Search provisional query projection; protected IME keys; explicit Conversation visual/focus ownership; no duplicate composition.'
