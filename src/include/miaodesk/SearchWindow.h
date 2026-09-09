#pragma once
#include "miaodesk/AppSearch.h"
#include "miaodesk/GozSearch.h"
#include "miaodesk/L3Agent.h"
#include "miaodesk/InputImeAnchor.h"
#include "miaodesk/InputImeSession.h"
#include "miaodesk/SearchTypes.h"
#include <windows.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace miaodesk {

class SearchWindow {
public:
    explicit SearchWindow(HINSTANCE instance);
    ~SearchWindow();

    bool Create(bool showOnLaunch = true);
    void ShowAndFocus();
    int RunMessageLoop();
    bool SelfTest();
    const std::wstring& LastCreateError() const noexcept { return lastCreateError_; }
    DWORD MessageLoopError() const noexcept { return messageLoopError_; }
    bool ExitExpected() const noexcept { return exiting_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void OnQueryChanged();
    void MergeResults();
    void ExecuteSelected(bool forceL3);
    void StartL3(const std::wstring& prompt);
    void StartWindowsVoiceTyping();
    void OpenSettingsCenter();
    void Draw();
    bool EnsureLayerSurface(UINT width, UINT height);
    void ReleaseLayerSurface();
    bool PresentLayerSurface(UINT width, UINT height);
    void ResizeRenderTarget(UINT width, UINT height);
    void PositionWindow();
    void LoadPosition();
    void SavePosition();
    void SetStatus(std::wstring title, std::wstring subtitle = {});
    void SetExpanded(bool expanded);
    void AddTray();
    void RemoveTray();
    void HandleTray(UINT mouseMessage);
    void ExitApplication();
    void UpdateFocusVisual();
    void SetHoverVisual(bool hovered);
    bool HitVoiceButton(POINT point) const;
    bool HitAiButton(POINT point) const;

    HINSTANCE instance_{};
    HWND hwnd_{};
    HWND edit_{};
    WNDPROC oldEditProc_{};
    AppSearch apps_;
    GozSearch files_;
    L3Agent l3_;
    std::vector<SearchResult> appResults_;
    std::vector<SearchResult> fileResults_;
    std::vector<SearchResult> results_;
    int selected_{-1};
    bool fileSearchAvailable_{false};
    bool fileSearchPending_{false};
    bool fileSearchQueryFailed_{false};
    bool expanded_{false};
    bool exiting_{false};
    bool positionLoaded_{false};
    bool editFocused_{false};
    bool hovered_{false};
    bool caretVisible_{true};
    bool hotkeyRegistered_{false};
    int savedX_{0};
    int savedY_{0};
    std::wstring currentQuery_;
    std::wstring lastCreateError_;
    DWORD messageLoopError_{};

    NOTIFYICONDATAW tray_{};
    bool trayAdded_{false};
    UINT taskbarCreated_{0};
    HFONT uiFont_{};
    HFONT smallFont_{};

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> renderTarget_;
    HDC layerDc_{};
    HBITMAP layerBitmap_{};
    HGDIOBJ layerOldBitmap_{};
    void* layerBits_{};
    UINT layerWidth_{};
    UINT layerHeight_{};

    Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> inputFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> subtitleFormat_;
};

} // namespace miaodesk
