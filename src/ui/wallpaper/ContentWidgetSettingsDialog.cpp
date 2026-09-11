#include "miaodesk/ContentWidgetSettingsDialog.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kSettingsClass[] = L"MiaoDesk.ContentWidget.Settings";
constexpr int kApplyId = 7101;
constexpr int kResetId = 7102;
constexpr int kCloseId = 7103;
constexpr int kFirstFieldId = 7200;

struct Field {
    content::ContentParameterDefinition definition;
    HWND control{};
    std::wstring initialState;
};

struct DialogState {
    HINSTANCE instance{};
    HWND owner{};
    HWND window{};
    HWND status{};
    HFONT font{};
    desktop::DesktopWidgetController* controller{};
    desktop::ContentWidgetSettingsSnapshot snapshot;
    std::vector<Field> fields;
    bool changed{};
    std::wstring resultMessage;
};

int S(HWND window, int px) {
    const UINT dpi = window ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window)) : USER_DEFAULT_SCREEN_DPI;
    return MulDiv(px, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

std::wstring FriendlyParameterName(std::wstring_view key) {
    if (key == L"backgroundColor") return L"背景颜色";
    if (key == L"accentColor") return L"强调颜色";
    if (key == L"textColor") return L"文字颜色";
    if (key == L"cardOpacity") return L"卡片透明度";
    if (key == L"showSeconds") return L"显示秒数";
    if (key == L"title") return L"标题";
    return std::wstring(key);
}

std::wstring NumericHint(const content::ContentParameterDefinition& parameter) {
    if (parameter.type != content::ContentParameterType::Int &&
        parameter.type != content::ContentParameterType::Float) return {};
    if (!parameter.minimum && !parameter.maximum && !parameter.step) return {};
    std::wostringstream text;
    text.imbue(std::locale::classic());
    text << L"  (";
    if (parameter.minimum) text << L"min " << *parameter.minimum;
    if (parameter.minimum && parameter.maximum) text << L", ";
    if (parameter.maximum) text << L"max " << *parameter.maximum;
    if ((parameter.minimum || parameter.maximum) && parameter.step) text << L", ";
    if (parameter.step) text << L"step " << *parameter.step;
    text << L")";
    return text.str();
}

std::wstring WindowText(HWND control) {
    if (!control) return {};
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

std::wstring Trim(std::wstring value) {
    const auto notSpace = [](wchar_t ch) { return !std::iswspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::wstring ColorText(const content::Color4& color) {
    const auto byte = [](double value) {
        return static_cast<unsigned>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };
    wchar_t text[16]{};
    swprintf_s(text, L"#%02X%02X%02X%02X", byte(color.r), byte(color.g), byte(color.b), byte(color.a));
    return text;
}

std::wstring ValueText(const content::ContentParameterValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return std::to_wstring(*integer);
    if (const auto* number = std::get_if<double>(&value)) {
        std::wostringstream text;
        text.imbue(std::locale::classic());
        text << std::setprecision(8) << *number;
        return text.str();
    }
    if (const auto* string = std::get_if<std::wstring>(&value)) return *string;
    if (const auto* color = std::get_if<content::Color4>(&value)) return ColorText(*color);
    if (const auto* asset = std::get_if<content::AssetReference>(&value)) return asset->id;
    return {};
}

bool HexNibble(wchar_t ch, unsigned* value) {
    if (!value) return false;
    if (ch >= L'0' && ch <= L'9') { *value = static_cast<unsigned>(ch - L'0'); return true; }
    if (ch >= L'a' && ch <= L'f') { *value = static_cast<unsigned>(10 + ch - L'a'); return true; }
    if (ch >= L'A' && ch <= L'F') { *value = static_cast<unsigned>(10 + ch - L'A'); return true; }
    return false;
}

bool ParseColor(std::wstring text, content::Color4* color) {
    if (!color) return false;
    text = Trim(std::move(text));
    if (!text.empty() && text.front() == L'#') text.erase(text.begin());
    if (text.size() != 6 && text.size() != 8) return false;
    std::vector<unsigned> bytes;
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned hi = 0;
        unsigned lo = 0;
        if (!HexNibble(text[i], &hi) || !HexNibble(text[i + 1], &lo)) return false;
        bytes.push_back((hi << 4u) | lo);
    }
    color->r = bytes[0] / 255.0;
    color->g = bytes[1] / 255.0;
    color->b = bytes[2] / 255.0;
    color->a = bytes.size() == 4 ? bytes[3] / 255.0 : 1.0;
    return true;
}

std::wstring ControlState(const Field& field) {
    if (!field.control) return {};
    if (field.definition.type == content::ContentParameterType::Bool)
        return SendMessageW(field.control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0";
    if (field.definition.type == content::ContentParameterType::Enum) {
        const LRESULT index = SendMessageW(field.control, CB_GETCURSEL, 0, 0);
        if (index == CB_ERR || index < 0 || static_cast<std::size_t>(index) >= field.definition.choices.size()) return {};
        return field.definition.choices[static_cast<std::size_t>(index)];
    }
    return WindowText(field.control);
}

bool ParseFieldValue(const Field& field, content::ContentParameterValue* value, std::wstring* error) {
    if (!value) return false;
    const auto& parameter = field.definition;
    if (parameter.type == content::ContentParameterType::Bool) {
        *value = SendMessageW(field.control, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return true;
    }
    if (parameter.type == content::ContentParameterType::Enum) {
        const LRESULT index = SendMessageW(field.control, CB_GETCURSEL, 0, 0);
        if (index == CB_ERR || index < 0 || static_cast<std::size_t>(index) >= parameter.choices.size()) {
            if (error) *error = FriendlyParameterName(parameter.key) + L" 没有选择有效值。";
            return false;
        }
        *value = parameter.choices[static_cast<std::size_t>(index)];
        return true;
    }

    std::wstring text = Trim(WindowText(field.control));
    if (parameter.type == content::ContentParameterType::String) {
        *value = std::move(text);
        return true;
    }
    if (parameter.type == content::ContentParameterType::Asset) {
        if (text.empty()) {
            if (error) *error = FriendlyParameterName(parameter.key) + L" 不能为空。";
            return false;
        }
        *value = content::AssetReference{std::move(text)};
        return true;
    }
    if (parameter.type == content::ContentParameterType::Color) {
        content::Color4 color;
        if (!ParseColor(text, &color)) {
            if (error) *error = FriendlyParameterName(parameter.key) + L" 需要 #RRGGBB 或 #RRGGBBAA 格式。";
            return false;
        }
        *value = color;
        return true;
    }
    if (parameter.type == content::ContentParameterType::Int) {
        wchar_t* end = nullptr;
        const long long parsed = std::wcstoll(text.c_str(), &end, 10);
        if (text.empty() || !end || *end != L'\0') {
            if (error) *error = FriendlyParameterName(parameter.key) + L" 需要整数。";
            return false;
        }
        const double number = static_cast<double>(parsed);
        if ((parameter.minimum && number < *parameter.minimum) ||
            (parameter.maximum && number > *parameter.maximum)) {
            if (error) *error = FriendlyParameterName(parameter.key) + L" 超出允许范围。";
            return false;
        }
        *value = static_cast<std::int64_t>(parsed);
        return true;
    }
    if (parameter.type == content::ContentParameterType::Float) {
        wchar_t* end = nullptr;
        const double parsed = std::wcstod(text.c_str(), &end);
        if (text.empty() || !end || *end != L'\0' || !std::isfinite(parsed)) {
            if (error) *error = FriendlyParameterName(parameter.key) + L" 需要数字。";
            return false;
        }
        if ((parameter.minimum && parsed < *parameter.minimum) ||
            (parameter.maximum && parsed > *parameter.maximum)) {
            if (error) *error = FriendlyParameterName(parameter.key) + L" 超出允许范围。";
            return false;
        }
        *value = parsed;
        return true;
    }
    if (error) *error = L"暂不支持该参数类型。";
    return false;
}

void SetFieldValue(Field& field, const content::ContentParameterValue& value) {
    if (!field.control) return;
    if (field.definition.type == content::ContentParameterType::Bool) {
        const auto* boolean = std::get_if<bool>(&value);
        SendMessageW(field.control, BM_SETCHECK, boolean && *boolean ? BST_CHECKED : BST_UNCHECKED, 0);
    } else if (field.definition.type == content::ContentParameterType::Enum) {
        const auto* selected = std::get_if<std::wstring>(&value);
        int selectedIndex = 0;
        if (selected) {
            const auto found = std::find(field.definition.choices.begin(), field.definition.choices.end(), *selected);
            if (found != field.definition.choices.end())
                selectedIndex = static_cast<int>(found - field.definition.choices.begin());
        }
        SendMessageW(field.control, CB_SETCURSEL, selectedIndex, 0);
    } else {
        const std::wstring text = ValueText(value);
        SetWindowTextW(field.control, text.c_str());
    }
    field.initialState = ControlState(field);
}

void SetStatus(DialogState& state, std::wstring text) {
    state.resultMessage = std::move(text);
    if (state.status) SetWindowTextW(state.status, state.resultMessage.c_str());
}

bool ReloadValues(DialogState& state) {
    desktop::ContentWidgetSettingsSnapshot refreshed;
    const auto result = state.controller->GetContentSettings(state.snapshot.widgetId, &refreshed);
    if (!result.success) {
        SetStatus(state, result.message);
        return false;
    }
    state.snapshot = std::move(refreshed);
    for (auto& field : state.fields) {
        const auto found = state.snapshot.values.find(field.definition.key);
        if (found != state.snapshot.values.end()) SetFieldValue(field, found->second);
    }
    return true;
}

void Apply(DialogState& state) {
    content::ContentParameterValues changes;
    std::wstring error;
    for (const auto& field : state.fields) {
        if (ControlState(field) == field.initialState) continue;
        content::ContentParameterValue value;
        if (!ParseFieldValue(field, &value, &error)) {
            SetStatus(state, error);
            MessageBeep(MB_ICONWARNING);
            return;
        }
        changes.emplace(field.definition.key, std::move(value));
    }
    if (changes.empty()) {
        SetStatus(state, L"没有需要应用的变化。");
        return;
    }

    const auto result = state.controller->SetContentParameters(state.snapshot.widgetId, changes);
    if (!result.success) {
        SetStatus(state, result.message);
        MessageBeep(MB_ICONERROR);
        return;
    }
    state.changed = true;
    ReloadValues(state);
    SetStatus(state, L"已原子应用。桌面组件会自动刷新，无需重启。");
}

void ResetDefaults(DialogState& state) {
    const auto result = state.controller->ResetContentParameters(state.snapshot.widgetId);
    if (!result.success) {
        SetStatus(state, result.message);
        MessageBeep(MB_ICONERROR);
        return;
    }
    state.changed = true;
    if (ReloadValues(state)) SetStatus(state, L"已恢复 package 默认值。");
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        if (state) {
            state->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kApplyId: Apply(*state); return 0;
        case kResetId: ResetDefaults(*state); return 0;
        case kCloseId: DestroyWindow(hwnd); return 0;
        default: break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        state->window = nullptr;
        return 0;
    default: break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ApplyFont(HWND control, HFONT font) {
    if (control && font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND MakeControl(DialogState& state, DWORD exStyle, const wchar_t* klass, const wchar_t* text,
                 DWORD style, int id, int x, int y, int w, int h) {
    HWND control = CreateWindowExW(
        exStyle, klass, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state.instance, nullptr);
    ApplyFont(control, state.font);
    return control;
}

void BuildControls(DialogState& state) {
    const int margin = S(state.window, 20);
    const int labelW = S(state.window, 180);
    const int controlW = S(state.window, 260);
    const int rowH = S(state.window, 50);
    int y = S(state.window, 18);

    HWND intro = MakeControl(state, 0, L"STATIC", state.snapshot.definition.name.c_str(),
                             SS_LEFT, 0, margin, y, labelW + controlW, S(state.window, 24));
    ApplyFont(intro, state.font);
    y += S(state.window, 38);

    state.fields.clear();
    state.fields.reserve(state.snapshot.definition.parameters.size());
    int fieldId = kFirstFieldId;
    for (const auto& parameter : state.snapshot.definition.parameters) {
        std::wstring label = FriendlyParameterName(parameter.key) + NumericHint(parameter);
        MakeControl(state, 0, L"STATIC", label.c_str(), SS_LEFT | SS_CENTERIMAGE,
                    0, margin, y, labelW, S(state.window, 30));

        HWND control{};
        if (parameter.type == content::ContentParameterType::Bool) {
            control = MakeControl(state, 0, L"BUTTON", L"启用", BS_AUTOCHECKBOX | WS_TABSTOP,
                                  fieldId, margin + labelW, y, controlW, S(state.window, 30));
        } else if (parameter.type == content::ContentParameterType::Enum) {
            control = MakeControl(state, WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                                  CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                  fieldId, margin + labelW, y, controlW, S(state.window, 180));
            for (const auto& choice : parameter.choices)
                SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.c_str()));
        } else {
            control = MakeControl(state, WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  ES_AUTOHSCROLL | WS_TABSTOP,
                                  fieldId, margin + labelW, y, controlW, S(state.window, 30));
        }

        Field field{parameter, control, {}};
        const auto value = state.snapshot.values.find(parameter.key);
        if (value != state.snapshot.values.end()) SetFieldValue(field, value->second);
        else field.initialState = ControlState(field);
        state.fields.push_back(std::move(field));
        ++fieldId;
        y += rowH;
    }

    state.status = MakeControl(state, 0, L"STATIC", L"修改后点击“应用”。",
                               SS_LEFT | SS_CENTERIMAGE, 0,
                               margin, y + S(state.window, 4), labelW + controlW, S(state.window, 28));
    y += S(state.window, 44);

    const int buttonW = S(state.window, 110);
    const int gap = S(state.window, 10);
    const int total = buttonW * 3 + gap * 2;
    const int left = margin + labelW + controlW - total;
    MakeControl(state, 0, L"BUTTON", L"恢复默认", BS_PUSHBUTTON | WS_TABSTOP,
                kResetId, left, y, buttonW, S(state.window, 34));
    MakeControl(state, 0, L"BUTTON", L"应用", BS_DEFPUSHBUTTON | WS_TABSTOP,
                kApplyId, left + buttonW + gap, y, buttonW, S(state.window, 34));
    MakeControl(state, 0, L"BUTTON", L"关闭", BS_PUSHBUTTON | WS_TABSTOP,
                kCloseId, left + (buttonW + gap) * 2, y, buttonW, S(state.window, 34));
}

void CenterOnOwner(HWND window, HWND owner) {
    RECT windowRect{};
    RECT ownerRect{};
    GetWindowRect(window, &windowRect);
    if (!owner || !IsWindow(owner) || !GetWindowRect(owner, &ownerRect)) {
        ownerRect = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
}

} // namespace

bool ShowContentWidgetSettingsDialog(
    HINSTANCE instance,
    HWND owner,
    desktop::DesktopWidgetController& controller,
    std::wstring_view widgetId,
    std::wstring* status) {
    desktop::ContentWidgetSettingsSnapshot snapshot;
    const auto loaded = controller.GetContentSettings(widgetId, &snapshot);
    if (!loaded.success) {
        if (status) *status = loaded.message;
        return false;
    }
    if (snapshot.definition.parameters.empty()) {
        if (status) *status = L"该 Content widget 没有可编辑参数。";
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = SettingsProc;
    wc.lpszClassName = kSettingsClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (status) *status = L"无法注册 Content widget 设置窗口。";
        return false;
    }

    DialogState state;
    state.instance = instance;
    state.owner = owner;
    state.controller = &controller;
    state.snapshot = std::move(snapshot);

    const int rowCount = static_cast<int>(state.snapshot.definition.parameters.size());
    const int width = 510;
    const int height = std::max(280, 172 + rowCount * 50);
    const std::wstring title = L"小组件设置 · " + state.snapshot.title;
    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        kSettingsClass, title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        owner, nullptr, instance, &state);
    if (!window) {
        if (status) *status = L"无法创建 Content widget 设置窗口。";
        return false;
    }

    const UINT dpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window));
    state.font = CreateFontW(-MulDiv(14, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI),
                             0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    BuildControls(state);
    CenterOnOwner(window, owner);

    if (owner && IsWindow(owner)) EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message{};
    while (state.window && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (state.font) DeleteObject(state.font);
    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (status) *status = state.resultMessage;
    return state.changed;
}

} // namespace miaodesk::wallpaper
