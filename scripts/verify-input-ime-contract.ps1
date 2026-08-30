$ErrorActionPreference = 'Stop'

$imeSource = 'src/native/include/miaodesk/InputImeAnchor.h'
$conversationOverlay = 'src/native/src/ui/ai/ConversationPanelInputOverlay.inc'
$conversationSource = 'src/native/src/ui/ai/ConversationPanel.cpp'
$legacyImeSource = 'src/native/include/miaodesk/SearchImeAnchor.h'

foreach ($path in @($imeSource, $conversationOverlay, $conversationSource)) {
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "Input IME contract source missing: $path"
    }
}
if (Test-Path $legacyImeSource -PathType Leaf) {
    throw 'SearchImeAnchor.h must not return; the shared framework is InputImeAnchor.h.'
}

$ime = Get-Content $imeSource -Raw
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

Write-Host 'InputImeAnchor contract OK: shared Input framework; explicit Search/Conversation profiles; full HWND geometry; one caret/IME publication path.'
