#include "miaodesk/GeneratedDesktopPreview.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace miaodesk {
namespace {

constexpr UINT kConversationPreviewArtifactMessage = WM_APP + 35;
constexpr wchar_t kConversationPreviewCoreProcProperty[] = L"MiaoDesk.Conversation.PreviewCoreProc";
constexpr wchar_t kSearchPreviewCoreProcProperty[] = L"MiaoDesk.Search.PreviewCoreProc";
constexpr wchar_t kSearchPreviewPanelProperty[] = L"MiaoDesk.Search.PreviewPanel";

LRESULT CALLBACK SearchPreviewBridgeProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kSearchPreviewCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);

    if (message == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        HWND panel = reinterpret_cast<HWND>(GetPropW(hwnd, kSearchPreviewPanelProperty));
        if (!IsWindow(panel)) panel = hwnd;
        if (preview::HandleGeneratedPreviewCopyData(panel, data)) return TRUE;
    }

    const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, kSearchPreviewCoreProcProperty);
        RemovePropW(hwnd, kSearchPreviewPanelProperty);
    }
    return result;
}

LRESULT CALLBACK ConversationPreviewBridgeProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto core = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kConversationPreviewCoreProcProperty));
    if (!core) return DefWindowProcW(hwnd, message, wParam, lParam);

    if (message == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (data && data->dwData == preview::kCopyDataTag && data->lpData) {
            const auto* text = static_cast<const wchar_t*>(data->lpData);
            const std::wstring previewId = fs::path(text).filename().wstring();
            if (!previewId.empty()) {
                auto* payload = new std::wstring(previewId);
                PostMessageW(hwnd, kConversationPreviewArtifactMessage, 0, reinterpret_cast<LPARAM>(payload));
            }
        }
        if (preview::HandleGeneratedPreviewCopyData(hwnd, data)) return TRUE;
    }

    const LRESULT result = CallWindowProcW(core, hwnd, message, wParam, lParam);
    if (message == WM_NCDESTROY) RemovePropW(hwnd, kConversationPreviewCoreProcProperty);
    return result;
}

void EnsureConversationPreviewBridge(HWND panel) {
    if (!panel) return;

    if (!GetPropW(panel, kConversationPreviewCoreProcProperty)) {
        ChangeWindowMessageFilterEx(panel, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
        const auto core = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(panel, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ConversationPreviewBridgeProc)));
        if (core) SetPropW(panel, kConversationPreviewCoreProcProperty, reinterpret_cast<HANDLE>(core));
    }

    HWND search = GetWindow(panel, GW_OWNER);
    if (!search || !IsWindow(search)) return;
    SetPropW(search, kSearchPreviewPanelProperty, reinterpret_cast<HANDLE>(panel));
    if (!GetPropW(search, kSearchPreviewCoreProcProperty)) {
        ChangeWindowMessageFilterEx(search, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
        const auto searchCore = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(search, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SearchPreviewBridgeProc)));
        if (searchCore) SetPropW(search, kSearchPreviewCoreProcProperty, reinterpret_cast<HANDLE>(searchCore));
    }
}

} // namespace
} // namespace miaodesk
