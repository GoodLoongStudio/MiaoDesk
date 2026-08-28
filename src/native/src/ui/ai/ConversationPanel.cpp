#include "turingdesk/ConversationPanel.h"
#include "turingdesk/WindowPlacementStore.h"
#include "turingdesk/StoreDemoExperience.h"
#include "turingdesk/GeneratedDesktopPreview.h"
#include "turingdesk/WidgetIntentComposer.h"
#include "ConversationPanelCompileCompat.h"
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

BOOL TuringDeskSkipLegacyLayerAlpha(HWND, COLORREF, BYTE, DWORD) {
    // ConversationPanel is presented with UpdateLayeredWindow + premultiplied alpha.
    // Uniform layered alpha would reintroduce the hard/dirty edge fixed in Search Bar.
    return TRUE;
}

int TuringDeskSkipLegacyWindowRegion(HWND, HRGN region, BOOL) {
    // Direct2D is the only owner of the visible rounded edge. The legacy GDI region is
    // intentionally discarded so integer clipping cannot fight per-pixel antialiasing.
    if (region) DeleteObject(region);
    return 1;
}

HRESULT TuringDeskSkipLegacyDwmWindowAttribute(HWND, DWMWINDOWATTRIBUTE, LPCVOID, DWORD) {
    // Layered windows must not also receive a DWM corner/backdrop surface. That second owner
    // leaves opaque corner fragments around the Direct2D alpha edge on real Windows ARM64.
    return S_OK;
}

} // namespace

// Keep the historical implementation body private while the public entry stays canonical.
// The second macro keeps the existing runtime-contract marker during the migration.
#define ShowConversationPanel ShowConversationPanelCore
#define ShowL3CliWindow ShowConversationPanel
#define SetLayeredWindowAttributes TuringDeskSkipLegacyLayerAlpha
#define SetWindowRgn TuringDeskSkipLegacyWindowRegion
#define DwmSetWindowAttribute TuringDeskSkipLegacyDwmWindowAttribute
#ifdef CS_DROPSHADOW
#undef CS_DROPSHADOW
#endif
#define CS_DROPSHADOW 0
#include "ConversationPanelImpl.inc"
#undef CS_DROPSHADOW
#undef DwmSetWindowAttribute
#undef SetWindowRgn
#undef SetLayeredWindowAttributes
#undef ShowL3CliWindow
#undef ShowConversationPanel

// rpcndr.h from the Windows SDK still defines `small` as a legacy IDL macro. It must not
// leak into modern C++ parameter names in the Direct2D renderer.
#ifdef small
#undef small
#endif

namespace turingdesk {
namespace {

bool gConversationCaretVisible = true;
constexpr wchar_t kConversationPlacementValue[] = L"PiAgentConversationPanel";
constexpr UINT_PTR kConversationPlacementSubclassId = 0x5444504Cu; // "TDPL"

LRESULT CALLBACK ConversationPlacementSubclass(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR) {
    if (message == WM_EXITSIZEMOVE || (message == WM_SHOWWINDOW && wParam == FALSE)) {
        window_placement::Save(hwnd, kConversationPlacementValue);
    }
    if (message == WM_NCDESTROY) {
        window_placement::Save(hwnd, kConversationPlacementValue);
        RemoveWindowSubclass(hwnd, ConversationPlacementSubclass, kConversationPlacementSubclassId);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

void EnsureConversationPlacementPersistence(HWND window) {
    if (!window) return;
    SetWindowSubclass(
        window, ConversationPlacementSubclass,
        kConversationPlacementSubclassId, 0);
}

BOOL TuringDeskPresentConversationLayered(
    HWND hwnd, HDC hdcDst, POINT* destination, SIZE* size,
    HDC hdcSrc, POINT* source, COLORREF colorKey,
    BLENDFUNCTION* blend, DWORD flags);

} // namespace
} // namespace turingdesk

#define UpdateLayeredWindow TuringDeskPresentConversationLayered
#include "ConversationPanelLayeredSurface.inc"
#undef UpdateLayeredWindow

namespace turingdesk {
namespace {

Microsoft::WRL::ComPtr<IDWriteTextLayout> ConversationRawInputLayout(
    const std::wstring& text, IDWriteTextFormat* format, float width, float height) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (!format || !gConversationSurface.dwrite) return layout;
    const wchar_t* content = text.empty() ? L" " : text.c_str();
    const UINT32 length = text.empty() ? 1u : static_cast<UINT32>(text.size());
    gConversationSurface.dwrite->CreateTextLayout(
        content, length, format, std::max(1.0f, width), std::max(1.0f, height),
        layout.GetAddressOf());
    return layout;
}

RECT ConversationInputHitRect(const ConversationState& state) {
    const auto& surface = gConversationSurface;
    const int inputContainerH = Px(state, 64);
    const int inputTop = static_cast<int>(surface.height) - inputContainerH - Px(state, 16);
    const int actionSize = Px(state, 34);
    const int micLeft = state.sendRect.left - Px(state, 8) - actionSize;
    const int attachLeft = micLeft - Px(state, 4) - actionSize;
    return RECT{
        Px(state, 30), inputTop + Px(state, 8),
        attachLeft - Px(state, 6), inputTop + inputContainerH - Px(state, 8)};
}

void FocusConversationInputAtPoint(ConversationState& state, POINT point) {
    if (!state.input) return;
    SetFocus(state.input);

    const std::wstring inputText = ReadText(state.input);
    UINT32 caretIndex = static_cast<UINT32>(inputText.size());
    if (!inputText.empty()) {
        const RECT hitRect = ConversationInputHitRect(state);
        const float inputLeft = static_cast<float>(Px(state, 38));
        const int inputContainerH = Px(state, 64);
        const int inputTop = static_cast<int>(gConversationSurface.height) - inputContainerH - Px(state, 16);
        const float layoutTop = inputTop + Px(state, 16.0f);
        const float layoutHeight = static_cast<float>(Px(state, 36));
        const float layoutWidth = static_cast<float>((std::max)(Px(state, 40), hitRect.right - Px(state, 38)));
        auto format = LayerFormat(static_cast<float>(Px(state, 15)));
        auto layout = ConversationRawInputLayout(inputText, format.Get(), layoutWidth, layoutHeight);
        if (layout) {
            BOOL trailing = FALSE;
            BOOL inside = FALSE;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(layout->HitTestPoint(
                    static_cast<float>(point.x) - inputLeft,
                    static_cast<float>(point.y) - layoutTop,
                    &trailing, &inside, &metrics))) {
                caretIndex = metrics.textPosition + (trailing ? metrics.length : 0u);
                caretIndex = (std::min)(caretIndex, static_cast<UINT32>(inputText.size()));
            }
        }
    }
    SendMessageW(state.input, EM_SETSEL, static_cast<WPARAM>(caretIndex), static_cast<LPARAM>(caretIndex));
    gConversationCaretVisible = true;
}

void DrawConversationInputFocusAndCaret() {
    auto* state = gConversationState;
    auto& surface = gConversationSurface;
    if (!state || !state->input || !surface.target || !surface.width || !surface.height) return;
    if (GetFocus() != state->input) return;

    auto* target = surface.target.Get();
    const float width = static_cast<float>(surface.width);
    const int inputContainerH = Px(*state, 64);
    const int inputTop = static_cast<int>(surface.height) - inputContainerH - Px(*state, 16);
    const auto inputBox = D2D1::RoundedRect(
        D2D1::RectF(
            static_cast<float>(Px(*state, 22)), static_cast<float>(inputTop),
            width - Px(*state, 22.0f), static_cast<float>(inputTop + inputContainerH)),
        static_cast<float>(Px(*state, 20)), static_cast<float>(Px(*state, 20)));

    target->BeginDraw();
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    auto focus = LayerBrush(target, 0x73A7FF, 0.78f);
    auto caret = LayerBrush(target, 0x4285F4, 0.96f);
    if (focus) {
        target->DrawRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(
                    inputBox.rect.left + 1.0f, inputBox.rect.top + 1.0f,
                    inputBox.rect.right - 1.0f, inputBox.rect.bottom - 1.0f),
                inputBox.radiusX - 1.0f, inputBox.radiusY - 1.0f),
            focus.Get(), 1.05f);
    }

    if (gConversationCaretVisible && caret) {
        const int actionSize = Px(*state, 34);
        const int micLeft = state->sendRect.left - Px(*state, 8) - actionSize;
        const int attachLeft = micLeft - Px(*state, 4) - actionSize;
        const float inputLeft = static_cast<float>(Px(*state, 38));
        const float inputRight = static_cast<float>(attachLeft - Px(*state, 8));
        const float layoutTop = inputTop + Px(*state, 16.0f);
        const float layoutHeight = static_cast<float>(Px(*state, 36));
        const std::wstring inputText = ReadText(state->input);
        auto format = LayerFormat(static_cast<float>(Px(*state, 15)));
        auto layout = ConversationRawInputLayout(inputText, format.Get(), inputRight - inputLeft, layoutHeight);

        float caretX = inputLeft;
        float caretTop = inputTop + Px(*state, 18.0f);
        float caretBottom = inputTop + Px(*state, 43.0f);
        if (layout && !inputText.empty()) {
            DWORD selectionStart = 0;
            DWORD selectionEnd = 0;
            SendMessageW(
                state->input, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
                reinterpret_cast<LPARAM>(&selectionEnd));
            const UINT32 caretIndex = (std::min)(
                static_cast<UINT32>(selectionEnd), static_cast<UINT32>(inputText.size()));
            const bool atEnd = caretIndex >= inputText.size();
            const UINT32 hitPosition = atEnd
                ? static_cast<UINT32>(inputText.size() - 1)
                : caretIndex;
            FLOAT hitX = 0.0f;
            FLOAT hitY = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(layout->HitTestTextPosition(
                    hitPosition, atEnd ? TRUE : FALSE, &hitX, &hitY, &metrics))) {
                caretX += hitX;
                caretTop = layoutTop + hitY + 1.0f;
                caretBottom = layoutTop + hitY + (std::max)(18.0f, metrics.height - 1.0f);
            }
        }
        caretX = (std::clamp)(caretX, inputLeft, inputRight - 1.0f);
        target->DrawLine(
            D2D1::Point2F(caretX, caretTop),
            D2D1::Point2F(caretX, caretBottom), caret.Get(), 1.25f);
    }

    const HRESULT drawResult = target->EndDraw();
    if (drawResult == D2DERR_RECREATE_TARGET) ReleaseConversationLayerSurface();
}

void LimitPremultipliedPixelAlpha(std::uint8_t* pixel, std::uint8_t maskAlpha) {
    const std::uint8_t oldAlpha = pixel[3];
    if (oldAlpha <= maskAlpha) return;
    if (oldAlpha == 0 || maskAlpha == 0) {
        pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
        return;
    }
    for (int channel = 0; channel < 3; ++channel) {
        pixel[channel] = static_cast<std::uint8_t>(
            (static_cast<unsigned int>(pixel[channel]) * maskAlpha + oldAlpha / 2u) / oldAlpha);
    }
    pixel[3] = maskAlpha;
}

void ApplyConversationRoundedAlphaMask() {
    auto* state = gConversationState;
    auto& surface = gConversationSurface;
    if (!state || !surface.bits || surface.width < 2 || surface.height < 2) return;

    const float radius = static_cast<float>(Px(*state, 24));
    const float left = 0.5f;
    const float top = 0.5f;
    const float right = static_cast<float>(surface.width) - 0.5f;
    const float bottom = static_cast<float>(surface.height) - 0.5f;
    const int edge = (std::min)(
        static_cast<int>(std::ceil(radius + 1.5f)),
        static_cast<int>((std::min)(surface.width, surface.height) / 2u));
    auto* pixels = static_cast<std::uint8_t*>(surface.bits);

    const auto maskCorner = [&](int xBegin, int xEnd, int yBegin, int yEnd, float cx, float cy) {
        for (int y = yBegin; y < yEnd; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            for (int x = xBegin; x < xEnd; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float dx = px - cx;
                const float dy = py - cy;
                const float distance = std::sqrt(dx * dx + dy * dy);
                const float coverage = (std::clamp)(radius + 0.5f - distance, 0.0f, 1.0f);
                const auto maskAlpha = static_cast<std::uint8_t>(std::lround(coverage * 255.0f));
                auto* pixel = pixels + (static_cast<std::size_t>(y) * surface.width + static_cast<std::size_t>(x)) * 4u;
                LimitPremultipliedPixelAlpha(pixel, maskAlpha);
            }
        }
    };

    maskCorner(0, edge, 0, edge, left + radius, top + radius);
    maskCorner(static_cast<int>(surface.width) - edge, static_cast<int>(surface.width), 0, edge,
               right - radius, top + radius);
    maskCorner(0, edge, static_cast<int>(surface.height) - edge, static_cast<int>(surface.height),
               left + radius, bottom - radius);
    maskCorner(static_cast<int>(surface.width) - edge, static_cast<int>(surface.width),
               static_cast<int>(surface.height) - edge, static_cast<int>(surface.height),
               right - radius, bottom - radius);
}

BOOL TuringDeskPresentConversationLayered(
    HWND hwnd, HDC hdcDst, POINT* destination, SIZE* size,
    HDC hdcSrc, POINT* source, COLORREF colorKey,
    BLENDFUNCTION* blend, DWORD flags) {
    DrawConversationInputFocusAndCaret();
    // Final premultiplied-alpha mask is the last owner of the four visible outer corners.
    // Any legacy/native rectangular residue outside the Direct2D rounded geometry becomes
    // true transparent pixels before UpdateLayeredWindow reaches DWM.
    ApplyConversationRoundedAlphaMask();
    return ::UpdateLayeredWindow(
        hwnd, hdcDst, destination, size, hdcSrc, source,
        colorKey, blend, flags);
}

} // namespace
} // namespace turingdesk

#include "ConversationPanelInputOverlay.inc"
#include "ConversationPanelCornerResize.inc"
#include "ConversationPanelImageIntent.inc"
#include "ConversationPanelPreviewBridge.inc"

namespace turingdesk {

bool ShowConversationPanel(HINSTANCE instance, HWND owner, L3Agent& agent, const std::wstring& initialPrompt) {
    const bool wasVisible = gConversationState && IsWindow(gConversationState->window) &&
                            IsWindowVisible(gConversationState->window);
    // Install the visible Direct2D surface and semantic bridge before the first model turn.
    const bool shown = ShowConversationPanelCore(instance, owner, agent, L"");
    if (!shown) return false;

    EnsureConversationInputOverlay(instance);
    if (gConversationState && IsWindow(gConversationState->window)) {
        EnsureConversationPlacementPersistence(gConversationState->window);
        if (!wasVisible) {
            window_placement::Restore(
                gConversationState->window, kConversationPlacementValue,
                Px(*gConversationState, 420), Px(*gConversationState, 420));
        }
        EnsureConversationActivityBridge(gConversationState->window);
        EnsureConversationCornerResizeBridge(gConversationState->window);
        EnsureConversationImageIntentBridge(*gConversationState);
        EnsureConversationPreviewBridge(gConversationState->window);
        PositionConversationInputProxy(*gConversationState);
        RenderConversationLayerSurface(*gConversationState);
        if (!Trim(initialPrompt).empty() && !gConversationState->busy && !gConversationState->pendingConfirmation) {
            SetWindowTextW(gConversationState->input, initialPrompt.c_str());
            SendPrompt(*gConversationState);
        }
        SetFocus(gConversationState->input);
        gConversationCaretVisible = true;
        RenderConversationLayerSurface(*gConversationState);
    }
    return true;
}

} // namespace turingdesk