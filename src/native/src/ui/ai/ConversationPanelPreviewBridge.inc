#include "turingdesk/GeneratedDesktopPreview.h"

namespace turingdesk {
namespace {

constexpr wchar_t kConversationPreviewCoreProcProperty[] = L"TuringDesk.Conversation.PreviewCoreProc";

LRESULT CALLBACK ConversationPreviewBridgeProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kConversationPreviewCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);

    if (message == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (preview::HandleGeneratedPreviewCopyData(hwnd, data)) return TRUE;
    }

    const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
    if (message == WM_NCDESTROY) RemovePropW(hwnd, kConversationPreviewCoreProcProperty);
    return result;
}

void EnsureConversationPreviewBridge(HWND panel) {
    if (!panel || GetPropW(panel, kConversationPreviewCoreProcProperty)) return;
    ChangeWindowMessageFilterEx(panel, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    const auto core = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(panel, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ConversationPreviewBridgeProc)));
    if (core) SetPropW(panel, kConversationPreviewCoreProcProperty, reinterpret_cast<HANDLE>(core));
}

} // namespace
} // namespace turingdesk
