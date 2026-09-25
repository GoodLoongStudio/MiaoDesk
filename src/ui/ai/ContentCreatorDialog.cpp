#include "miaodesk/ContentCreatorDialog.h"

#include "miaodesk/NativeTools.h"
#include "miaodesk/NativeUiScale.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace miaodesk::creator {
namespace {

constexpr wchar_t kWindowClass[] = L"MiaoDesk.Native.ContentCreatorDialog";
constexpr int kTranscriptId = 7801;
constexpr int kPromptId = 7802;
constexpr int kSendId = 7803;
constexpr int kClearId = 7804;
constexpr int kSkillListId = 7805;
constexpr int kSkillTextId = 7806;
constexpr int kPreset1Id = 7810;
constexpr int kPreset2Id = 7811;
constexpr int kPreset3Id = 7812;
constexpr int kPreset4Id = 7813;
constexpr int kPreviewId = 7820;
constexpr int kLibraryId = 7821;
constexpr int kApplyId = 7822;
constexpr UINT kAppendDelta = WM_APP + 0x311;
constexpr UINT kRequestDone = WM_APP + 0x312;

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

bool Valid(ContentCreatorKind kind) noexcept {
    return kind == ContentCreatorKind::Wallpaper || kind == ContentCreatorKind::Widget;
}

std::wstring ReadText(HWND control) {
    const int length = control ? GetWindowTextLengthW(control) : 0;
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

void AppendText(HWND edit, std::wstring_view text) {
    if (!edit || text.empty()) return;
    const int length = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, length, length);
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(std::wstring(text).c_str()));
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

struct SkillItem {
    const wchar_t* label;
    const char* name;
};

constexpr std::array<SkillItem, 3> kWallpaperSkills{{
    {L"1. 内容包基础 · content-package-basics", "content-package-basics"},
    {L"2. 壁纸规则 · wallpaper-content", "wallpaper-content"},
    {L"3. 交付检查 · content-review", "content-review"},
}};
constexpr std::array<SkillItem, 3> kWidgetSkills{{
    {L"1. 内容包基础 · content-package-basics", "content-package-basics"},
    {L"2. 组件规则 · widget-content", "widget-content"},
    {L"3. 交付检查 · content-review", "content-review"},
}};

constexpr std::array<const wchar_t*, 4> kWallpaperPresets{{
    L"治愈天空与云层，轻微动态，不遮挡桌面图标",
    L"夜间霓虹城市，低干扰动态光效",
    L"极简深色动态壁纸，适合长期办公",
    L"猫咪主题壁纸，猫本体、尾巴、眨眼和云层分层动画保持对齐",
}};
constexpr std::array<const wchar_t*, 4> kWidgetPresets{{
    L"制作一个玻璃天气组件，信息清楚，适合桌面常驻",
    L"制作一个极简时钟组件，支持时间和日期",
    L"制作一个待办事项组件，支持完成状态",
    L"制作一个桌面宠物信息卡组件，保持轻量和低干扰",
}};

struct DialogState {
    HINSTANCE instance{};
    HWND owner{};
    HWND window{};
    L3Agent* agent{};
    ContentCreatorKind kind{ContentCreatorKind::None};
    HWND heading{};
    HWND note{};
    HWND transcript{};
    HWND prompt{};
    HWND send{};
    HWND clear{};
    HWND skillHeading{};
    HWND skillList{};
    HWND skillText{};
    HWND resultNote{};
    HWND preview{};
    HWND library{};
    HWND apply{};
    std::array<HWND, 4> presets{};
    HFONT bodyFont{};
    HFONT titleFont{};
    HFONT smallFont{};
    UINT fontScaleDpi{};
    bool primed{};
    bool busy{};

    ~DialogState() {
        if (bodyFont) DeleteObject(bodyFont);
        if (titleFont) DeleteObject(titleFont);
        if (smallFont) DeleteObject(smallFont);
    }

    bool IsWidget() const noexcept { return kind == ContentCreatorKind::Widget; }
    int S(int value) const noexcept {
        const UINT dpi = window ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window))
                                : USER_DEFAULT_SCREEN_DPI;
        return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    }

    const std::array<SkillItem, 3>& Skills() const noexcept {
        return IsWidget() ? kWidgetSkills : kWallpaperSkills;
    }
    const std::array<const wchar_t*, 4>& PresetText() const noexcept {
        return IsWidget() ? kWidgetPresets : kWallpaperPresets;
    }

    void RebuildFonts() {
        fontScaleDpi = ui::EffectiveFontDpi(window);
        if (bodyFont) DeleteObject(bodyFont);
        if (titleFont) DeleteObject(titleFont);
        if (smallFont) DeleteObject(smallFont);
        bodyFont = ui::CreateUiFont(window, 14, FW_NORMAL);
        titleFont = ui::CreateUiFont(window, 18, FW_SEMIBOLD, L"Segoe UI Variable Display");
        smallFont = ui::CreateUiFont(window, 12, FW_NORMAL);
    }

    void ApplyFonts() const {
        if (heading && titleFont) SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont), TRUE);
        for (HWND child : {note, transcript, prompt, send, clear, skillHeading, skillList, skillText,
                           resultNote, preview, library, apply}) {
            if (child && bodyFont) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
        }
        for (HWND child : presets)
            if (child && smallFont) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    }

    void RefreshFontScaleIfNeeded() {
        if (!window || !bodyFont || ui::EffectiveFontDpi(window) == fontScaleDpi) return;
        RebuildFonts();
        ApplyFonts();
        Layout();
    }

    void Layout() const {
        if (!window) return;
        RECT rc{};
        GetClientRect(window, &rc);
        const int width = std::max(1, static_cast<int>(rc.right - rc.left));
        const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));
        const auto logical = ResolveContentCreatorLayout(
            MulDiv(width, USER_DEFAULT_SCREEN_DPI, static_cast<int>(std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window)))),
            MulDiv(height, USER_DEFAULT_SCREEN_DPI, static_cast<int>(std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window)))));
        const int margin = S(logical.margin);
        const int gap = S(logical.gap);
        const int headerH = S(logical.headerHeight);
        const int footerH = S(logical.footerHeight);
        const int leftW = S(logical.leftWidth);
        const int rightW = std::max(S(250), width - margin * 2 - gap - leftW);
        const int bodyTop = margin + headerH + gap;
        const int footerTop = height - margin - footerH;
        const int bodyH = std::max(S(220), footerTop - gap - bodyTop);

        auto place = [](HWND child, int x, int y, int w, int h) {
            if (child) SetWindowPos(child, nullptr, x, y, std::max(1, w), std::max(1, h), SWP_NOZORDER | SWP_NOACTIVATE);
        };
        place(heading, margin, margin, width - margin * 2, S(30));
        place(note, margin, margin + S(32), width - margin * 2, S(28));

        const int presetH = S(30);
        const int promptH = S(86);
        const int actionsH = S(38);
        const int presetTop = footerTop - gap - presetH;
        const int promptTop = presetTop - gap - promptH;
        const int transcriptH = std::max(S(100), promptTop - gap - bodyTop);
        place(transcript, margin, bodyTop, leftW, transcriptH);
        place(prompt, margin, promptTop, leftW - S(186), promptH);
        place(send, margin + leftW - S(178), promptTop, S(86), actionsH);
        place(clear, margin + leftW - S(86), promptTop, S(86), actionsH);
        const int presetW = std::max(S(90), (leftW - gap * 3) / 4);
        for (int i = 0; i < 4; ++i)
            place(presets[static_cast<std::size_t>(i)], margin + i * (presetW + gap), presetTop, presetW, presetH);

        const int rightX = margin + leftW + gap;
        place(skillHeading, rightX, bodyTop, rightW, S(30));
        place(skillList, rightX, bodyTop + S(34), rightW, S(96));
        place(skillText, rightX, bodyTop + S(138), rightW, std::max(S(100), bodyH - S(180)));
        place(resultNote, rightX, bodyTop + bodyH - S(34), rightW, S(30));

        const int buttonW = S(128);
        place(preview, margin, footerTop, buttonW, footerH);
        place(library, margin + buttonW + gap, footerTop, buttonW, footerH);
        place(apply, margin + (buttonW + gap) * 2, footerTop, S(150), footerH);
    }

    void LoadSkill() const {
        if (!skillList || !skillText) return;
        LRESULT selected = SendMessageW(skillList, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR) selected = 0;
        if (selected < 0 || static_cast<std::size_t>(selected) >= Skills().size()) return;
        const auto& skill = Skills()[static_cast<std::size_t>(selected)];
        const std::string args = std::string("{\"name\":\"") + skill.name + "\"}";
        const auto result = ExecuteNativeToolRaw("content_skill_get", args);
        const std::wstring text = result.success ? result.message : L"无法读取 Skill：\r\n" + result.message;
        SetWindowTextW(skillText, text.c_str());
        SendMessageW(skillText, EM_SETSEL, 0, 0);
    }

    void SetBusy(bool value) {
        busy = value;
        EnableWindow(send, !value);
        SetWindowTextW(send, value ? L"生成中…" : L"生成");
    }

    std::wstring BuildPrompt(std::wstring_view userText) {
        std::wstring request;
        if (!primed) {
            request = InitialPrompt(kind);
            request += L"\n\n当前这个窗口是独立的轻量创作界面。请保持创作结果 preview-first，并在产出有效内容包后明确告诉我生成路径。\n\n用户需求：";
            primed = true;
        } else {
            request = L"继续当前";
            request += IsWidget() ? L"组件" : L"壁纸";
            request += L"创作任务。保持 preview-first，不要未经确认直接应用。用户补充：";
        }
        request.append(userText);
        return request;
    }

    void SendPrompt() {
        if (!agent || busy) return;
        std::wstring text = ReadText(prompt);
        if (text.empty()) return;
        AppendText(transcript, L"\r\n你：" + text + L"\r\n\r\n妙喵：");
        SetWindowTextW(prompt, L"");
        SetBusy(true);
        agent->ReloadConfig();
        const HWND target = window;
        agent->AskAsync(
            BuildPrompt(text),
            [target](std::wstring delta) {
                auto* heap = new std::wstring(std::move(delta));
                if (!PostMessageW(target, kAppendDelta, 0, reinterpret_cast<LPARAM>(heap))) delete heap;
            },
            [target](std::wstring done) {
                auto* heap = new std::wstring(std::move(done));
                if (!PostMessageW(target, kRequestDone, 0, reinterpret_cast<LPARAM>(heap))) delete heap;
            });
    }

    void ResetSession() {
        if (agent && agent->Busy()) agent->Stop();
        primed = false;
        SetBusy(false);
        SetWindowTextW(transcript,
            IsWidget()
                ? L"妙喵：描述你想制作的桌面组件。我会按右侧 Skills 生成可预览的 .mdwidget。\r\n"
                : L"妙喵：描述你想制作的壁纸。我会按右侧 Skills 生成可预览的 .mdwall。\r\n");
        SetWindowTextW(resultNote, L"尚未生成内容包 · 生成后先预览，再加入库或应用");
        EnableWindow(preview, FALSE);
        EnableWindow(library, FALSE);
        EnableWindow(apply, FALSE);
    }

    bool CreateControls() {
        RebuildFonts();
        auto label = [&](const wchar_t* text) {
            return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   0, 0, 10, 10, window, nullptr, instance, nullptr);
        };
        auto button = [&](const wchar_t* text, int id) {
            return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, 0, 10, 10, window, ControlId(id), instance, nullptr);
        };

        heading = label(IsWidget() ? L"AI 制作组件" : L"AI 制作壁纸");
        note = label(IsWidget()
            ? L"描述需求 → 按组件 Skills 生成 → 预览 → 加入组件库 → 添加到桌面"
            : L"描述需求 → 按壁纸 Skills 生成 → 预览 → 加入壁纸库 → 应用到桌面");
        transcript = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 10, 10, window, ControlId(kTranscriptId), instance, nullptr);
        prompt = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 10, 10, window, ControlId(kPromptId), instance, nullptr);
        SendMessageW(prompt, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(
            IsWidget() ? L"例如：做一个玻璃天气组件…" : L"例如：做一个治愈系猫咪动态壁纸…"));
        send = button(L"生成", kSendId);
        clear = button(L"新对话", kClearId);
        for (int i = 0; i < 4; ++i)
            presets[static_cast<std::size_t>(i)] = button(PresetText()[static_cast<std::size_t>(i)], kPreset1Id + i);
        skillHeading = label(IsWidget()
            ? L"当前 Skills · 输出 .mdwidget · preview-first"
            : L"当前 Skills · 输出 .mdwall · preview-first");
        skillList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 10, 10, window, ControlId(kSkillListId), instance, nullptr);
        skillText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            0, 0, 10, 10, window, ControlId(kSkillTextId), instance, nullptr);
        resultNote = label(L"尚未生成内容包 · 生成后先预览，再加入库或应用");
        preview = button(L"预览", kPreviewId);
        library = button(IsWidget() ? L"加入组件库" : L"加入壁纸库", kLibraryId);
        apply = button(IsWidget() ? L"添加到桌面" : L"应用到桌面", kApplyId);
        EnableWindow(preview, FALSE);
        EnableWindow(library, FALSE);
        EnableWindow(apply, FALSE);

        for (const auto& item : Skills())
            SendMessageW(skillList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.label));
        SendMessageW(skillList, LB_SETCURSEL, 0, 0);
        ApplyFonts();
        ResetSession();
        LoadSkill();
        Layout();
        return heading && note && transcript && prompt && send && clear && skillList && skillText;
    }
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        if (!state) return FALSE;
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        return state->CreateControls() ? 0 : -1;
    case WM_SIZE:
        state->Layout();
        return 0;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                                    SWP_NOZORDER | SWP_NOACTIVATE);
        state->RefreshFontScaleIfNeeded();
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == kSendId && HIWORD(wParam) == BN_CLICKED) { state->SendPrompt(); return 0; }
        if (id == kClearId && HIWORD(wParam) == BN_CLICKED) { state->ResetSession(); return 0; }
        if (id == kSkillListId && HIWORD(wParam) == LBN_SELCHANGE) { state->LoadSkill(); return 0; }
        if (id >= kPreset1Id && id <= kPreset4Id && HIWORD(wParam) == BN_CLICKED) {
            SetWindowTextW(state->prompt, state->PresetText()[static_cast<std::size_t>(id - kPreset1Id)]);
            SetFocus(state->prompt);
            return 0;
        }
        if ((id == kPreviewId || id == kLibraryId || id == kApplyId) && HIWORD(wParam) == BN_CLICKED) {
            MessageBoxW(hwnd, L"生成有效内容包后此操作会自动启用；当前仍保持 preview-first。",
                        L"妙喵 AI", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    }
    case kAppendDelta: {
        std::unique_ptr<std::wstring> delta(reinterpret_cast<std::wstring*>(lParam));
        if (delta) AppendText(state->transcript, *delta);
        return 0;
    }
    case kRequestDone: {
        std::unique_ptr<std::wstring> done(reinterpret_cast<std::wstring*>(lParam));
        state->SetBusy(false);
        AppendText(state->transcript, L"\r\n");
        SetWindowTextW(state->resultNote,
            done && !done->empty()
                ? L"本轮生成已完成 · 检查对话中的内容包路径后执行预览"
                : L"本轮请求结束 · 未检测到可操作的内容包");
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state->agent && state->agent->Busy()) state->agent->Stop();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

ContentCreatorLayout ResolveContentCreatorLayout(int clientWidth, int clientHeight) noexcept {
    clientWidth = std::max(760, clientWidth);
    clientHeight = std::max(560, clientHeight);
    ContentCreatorLayout layout{};
    layout.margin = 18;
    layout.gap = 14;
    layout.headerHeight = 64;
    layout.footerHeight = 42;
    const int usable = clientWidth - layout.margin * 2 - layout.gap;
    layout.leftWidth = std::clamp(usable * 58 / 100, 430, 720);
    layout.rightWidth = std::max(250, usable - layout.leftWidth);
    layout.bodyHeight = std::max(220, clientHeight - layout.margin * 2 - layout.headerHeight - layout.footerHeight - layout.gap * 2);
    return layout;
}

bool ShowContentCreatorDialog(HINSTANCE instance, HWND owner, L3Agent& agent, ContentCreatorKind kind) {
    if (!instance || !Valid(kind)) return false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    auto* state = new DialogState{};
    state->instance = instance;
    state->owner = owner;
    state->agent = &agent;
    state->kind = kind;

    const wchar_t* title = kind == ContentCreatorKind::Widget ? L"妙喵 · AI 制作组件" : L"妙喵 · AI 制作壁纸";
    HWND window = CreateWindowExW(
        WS_EX_APPWINDOW, kWindowClass, title,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1080, 760,
        owner, nullptr, instance, state);
    if (!window) {
        delete state;
        return false;
    }
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    return true;
}

} // namespace miaodesk::creator
