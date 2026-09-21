#include "miaodesk/TodayTaskEditorDialog.h"

#include <commctrl.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace miaodesk::wallpaper {
namespace {

constexpr wchar_t kEditorClass[] = L"MiaoDesk.TodayTasks.Editor";
constexpr wchar_t kItemClass[] = L"MiaoDesk.TodayTasks.ItemEditor";

constexpr int kListId = 7301;
constexpr int kAddId = 7302;
constexpr int kEditId = 7303;
constexpr int kToggleId = 7304;
constexpr int kDeleteId = 7305;
constexpr int kCloseId = 7306;
constexpr int kStatusId = 7307;

constexpr int kItemTitleId = 7351;
constexpr int kItemDetailId = 7352;
constexpr int kItemOkId = IDOK;
constexpr int kItemCancelId = IDCANCEL;

int S(HWND window, int px) {
    const UINT dpi = window ? std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window)) : USER_DEFAULT_SCREEN_DPI;
    return MulDiv(px, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
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
    const auto notSpace = [](wchar_t ch) {
        return ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n';
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

struct ItemDialogState {
    HINSTANCE instance{};
    HWND owner{};
    HWND window{};
    HWND titleEdit{};
    HWND detailEdit{};
    HFONT font{};
    std::wstring title;
    std::wstring detail;
    bool accepted{};
};

void CenterOverOwner(HWND window, HWND owner) {
    RECT rc{};
    RECT ownerRc{};
    if (!GetWindowRect(window, &rc)) return;
    if (!owner || !GetWindowRect(owner, &ownerRc)) {
        ownerRc = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - width) / 2;
    const int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - height) / 2;
    SetWindowPos(window, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
}

LRESULT CALLBACK ItemProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<ItemDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<ItemDialogState*>(create->lpCreateParams);
        if (state) {
            state->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wParam) == kItemOkId) {
            const std::wstring title = Trim(WindowText(state->titleEdit));
            if (title.empty()) {
                MessageBoxW(hwnd, L"待办标题不能为空。", L"妙喵", MB_OK | MB_ICONWARNING);
                SetFocus(state->titleEdit);
                return 0;
            }
            state->title = title;
            state->detail = Trim(WindowText(state->detailEdit));
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == kItemCancelId) {
            DestroyWindow(hwnd);
            return 0;
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

bool EditTaskItem(HINSTANCE instance, HWND owner, std::wstring* title, std::wstring* detail) {
    if (!title || !detail) return false;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = ItemProc;
    wc.lpszClassName = kItemClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    ItemDialogState state;
    state.instance = instance;
    state.owner = owner;
    state.title = *title;
    state.detail = *detail;

    state.window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kItemClass, title->empty() ? L"新增待办" : L"编辑待办",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 300,
        owner, nullptr, instance, &state);
    if (!state.window) return false;

    const int margin = S(state.window, 18);
    const int width = S(state.window, 520);
    const int labelH = S(state.window, 22);
    const int editH = S(state.window, 34);
    const int detailH = S(state.window, 74);
    state.font = CreateFontW(-S(state.window, 14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    auto make = [&](const wchar_t* klass, const wchar_t* text, DWORD style, int id,
                    int x, int y, int w, int h) {
        HWND control = CreateWindowExW(klass == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0,
                                       klass, text, WS_CHILD | WS_VISIBLE | style,
                                       x, y, w, h, state.window,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (control && state.font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        return control;
    };

    int y = margin;
    make(L"STATIC", L"标题", SS_LEFT, 0, margin, y, width, labelH); y += labelH;
    state.titleEdit = make(L"EDIT", state.title.c_str(), ES_AUTOHSCROLL | WS_TABSTOP,
                           kItemTitleId, margin, y, width, editH); y += editH + S(state.window, 12);
    make(L"STATIC", L"详情 / 时间", SS_LEFT, 0, margin, y, width, labelH); y += labelH;
    state.detailEdit = make(L"EDIT", state.detail.c_str(), ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
                            kItemDetailId, margin, y, width, detailH); y += detailH + S(state.window, 16);
    const int buttonW = S(state.window, 92);
    make(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, kItemCancelId,
         margin + width - buttonW * 2 - S(state.window, 8), y, buttonW, S(state.window, 34));
    make(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, kItemOkId,
         margin + width - buttonW, y, buttonW, S(state.window, 34));

    RECT desired{0, 0, margin * 2 + width, y + S(state.window, 34) + margin};
    AdjustWindowRectExForDpi(&desired, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE,
                             WS_EX_DLGMODALFRAME, GetDpiForWindow(state.window));
    SetWindowPos(state.window, nullptr, 0, 0, desired.right - desired.left, desired.bottom - desired.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CenterOverOwner(state.window, owner);
    EnableWindow(owner, FALSE);
    ShowWindow(state.window, SW_SHOWNORMAL);
    SetFocus(state.titleEdit);

    MSG message{};
    while (state.window && IsWindow(state.window)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(state.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (state.font) DeleteObject(state.font);
    if (!state.accepted) return false;
    *title = std::move(state.title);
    *detail = std::move(state.detail);
    return true;
}

struct EditorState {
    HINSTANCE instance{};
    HWND owner{};
    HWND window{};
    HWND list{};
    HWND status{};
    HFONT font{};
    desktop::DesktopWidgetController* controller{};
    desktop::TodayTaskSnapshot snapshot;
    bool changed{};
    std::wstring resultMessage;
};

void SetStatus(EditorState& state, std::wstring message) {
    state.resultMessage = std::move(message);
    if (state.status) SetWindowTextW(state.status, state.resultMessage.c_str());
}

int SelectedIndex(const EditorState& state) {
    if (!state.list) return -1;
    return ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
}

void RefreshList(EditorState& state) {
    desktop::TodayTaskSnapshot snapshot;
    const auto result = state.controller->GetTodayTasks(&snapshot);
    if (!result.success) {
        SetStatus(state, result.message);
        return;
    }
    state.snapshot = std::move(snapshot);
    ListView_DeleteAllItems(state.list);
    for (std::size_t i = 0; i < state.snapshot.items.size(); ++i) {
        const auto& task = state.snapshot.items[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        std::wstring status = task.completed ? L"完成" : L"待办";
        item.pszText = status.data();
        ListView_InsertItem(state.list, &item);
        ListView_SetItemText(state.list, static_cast<int>(i), 1, const_cast<wchar_t*>(task.title.c_str()));
        ListView_SetItemText(state.list, static_cast<int>(i), 2, const_cast<wchar_t*>(task.detail.c_str()));
    }
    SetStatus(state, L"共 " + std::to_wstring(state.snapshot.total) + L" 项 · 已完成 " +
                     std::to_wstring(state.snapshot.completed) + L" · 待办 " +
                     std::to_wstring(state.snapshot.pending));
}

bool SaveItems(EditorState& state, std::vector<desktop::TodayTaskItem> items) {
    const auto result = state.controller->ReplaceTodayTasks(items);
    if (!result.success) {
        SetStatus(state, result.message);
        MessageBeep(MB_ICONERROR);
        return false;
    }
    state.changed = true;
    RefreshList(state);
    return true;
}

void AddTask(EditorState& state) {
    std::wstring title;
    std::wstring detail;
    if (!EditTaskItem(state.instance, state.window, &title, &detail)) return;
    auto items = state.snapshot.items;
    desktop::TodayTaskItem item;
    item.id = L"task-" + std::to_wstring(GetTickCount64());
    item.title = std::move(title);
    item.detail = std::move(detail);
    items.push_back(std::move(item));
    if (SaveItems(state, std::move(items)) && !state.snapshot.items.empty()) {
        const int index = static_cast<int>(state.snapshot.items.size() - 1);
        ListView_SetItemState(state.list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(state.list, index, FALSE);
    }
}

void EditSelected(EditorState& state) {
    const int index = SelectedIndex(state);
    if (index < 0 || static_cast<std::size_t>(index) >= state.snapshot.items.size()) return;
    auto items = state.snapshot.items;
    std::wstring title = items[static_cast<std::size_t>(index)].title;
    std::wstring detail = items[static_cast<std::size_t>(index)].detail;
    if (!EditTaskItem(state.instance, state.window, &title, &detail)) return;
    items[static_cast<std::size_t>(index)].title = std::move(title);
    items[static_cast<std::size_t>(index)].detail = std::move(detail);
    if (SaveItems(state, std::move(items)))
        ListView_SetItemState(state.list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void ToggleSelected(EditorState& state) {
    const int index = SelectedIndex(state);
    if (index < 0 || static_cast<std::size_t>(index) >= state.snapshot.items.size()) return;
    const auto& task = state.snapshot.items[static_cast<std::size_t>(index)];
    const auto result = state.controller->SetTodayTaskCompleted(task.id, !task.completed);
    if (!result.success) {
        SetStatus(state, result.message);
        MessageBeep(MB_ICONERROR);
        return;
    }
    state.changed = true;
    RefreshList(state);
    ListView_SetItemState(state.list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void DeleteSelected(EditorState& state) {
    const int index = SelectedIndex(state);
    if (index < 0 || static_cast<std::size_t>(index) >= state.snapshot.items.size()) return;
    auto items = state.snapshot.items;
    items.erase(items.begin() + index);
    if (SaveItems(state, std::move(items)) && !state.snapshot.items.empty()) {
        const int next = std::min(index, static_cast<int>(state.snapshot.items.size() - 1));
        ListView_SetItemState(state.list, next, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void LayoutEditor(EditorState& state) {
    if (!state.window) return;
    RECT rc{};
    GetClientRect(state.window, &rc);
    const int margin = S(state.window, 16);
    const int buttonH = S(state.window, 36);
    const int statusH = S(state.window, 28);
    const int gap = S(state.window, 8);
    const int buttonW = S(state.window, 96);
    const int listBottom = rc.bottom - margin - statusH - gap - buttonH - gap;
    MoveWindow(state.list, margin, margin, std::max(1, rc.right - margin * 2), std::max(1, listBottom - margin), TRUE);
    MoveWindow(state.status, margin, listBottom + gap, std::max(1, rc.right - margin * 2), statusH, TRUE);
    int x = margin;
    for (const int id : {kAddId, kEditId, kToggleId, kDeleteId}) {
        HWND button = GetDlgItem(state.window, id);
        MoveWindow(button, x, rc.bottom - margin - buttonH, buttonW, buttonH, TRUE);
        x += buttonW + gap;
    }
    MoveWindow(GetDlgItem(state.window, kCloseId), rc.right - margin - buttonW,
               rc.bottom - margin - buttonH, buttonW, buttonH, TRUE);

    const int listW = std::max(1, rc.right - margin * 2 - GetSystemMetrics(SM_CXVSCROLL) - S(state.window, 8));
    ListView_SetColumnWidth(state.list, 0, S(state.window, 70));
    ListView_SetColumnWidth(state.list, 1, std::max(S(state.window, 180), listW * 45 / 100));
    ListView_SetColumnWidth(state.list, 2, LVSCW_AUTOSIZE_USEHEADER);
}

LRESULT CALLBACK EditorProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<EditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<EditorState*>(create->lpCreateParams);
        if (state) {
            state->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_SIZE:
        LayoutEditor(*state);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kAddId: AddTask(*state); return 0;
        case kEditId: EditSelected(*state); return 0;
        case kToggleId: ToggleSelected(*state); return 0;
        case kDeleteId: DeleteSelected(*state); return 0;
        case kCloseId: DestroyWindow(hwnd); return 0;
        default: break;
        }
        break;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->idFrom == kListId && header->code == NM_DBLCLK) {
            EditSelected(*state);
            return 0;
        }
        break;
    }
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

} // namespace

bool ShowTodayTaskEditorDialog(
    HINSTANCE instance,
    HWND owner,
    desktop::DesktopWidgetController& controller,
    std::wstring* resultMessage) {
    if (resultMessage) resultMessage->clear();
    if (!instance || !owner || !IsWindow(owner)) return false;

    INITCOMMONCONTROLSEX common{sizeof(common), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = EditorProc;
    wc.lpszClassName = kEditorClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    EditorState state;
    state.instance = instance;
    state.owner = owner;
    state.controller = &controller;
    state.window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kEditorClass, L"今日待办",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 780, 560,
        owner, nullptr, instance, &state);
    if (!state.window) return false;

    state.font = CreateFontW(-S(state.window, 14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    state.list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, state.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), instance, nullptr);
    ListView_SetExtendedListViewStyle(state.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    if (state.font) SendMessageW(state.list, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
    const wchar_t* headings[] = {L"状态", L"标题", L"详情 / 时间"};
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.iSubItem = i;
        column.cx = i == 0 ? S(state.window, 70) : S(state.window, 250);
        column.pszText = const_cast<wchar_t*>(headings[i]);
        ListView_InsertColumn(state.list, i, &column);
    }

    auto makeButton = [&](const wchar_t* text, int id) {
        HWND button = CreateWindowExW(0, L"BUTTON", text,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      0, 0, 10, 10, state.window,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (state.font) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        return button;
    };
    makeButton(L"新增", kAddId);
    makeButton(L"编辑", kEditId);
    makeButton(L"完成 / 恢复", kToggleId);
    makeButton(L"删除", kDeleteId);
    makeButton(L"关闭", kCloseId);
    state.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   0, 0, 10, 10, state.window,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusId)), instance, nullptr);
    if (state.font) SendMessageW(state.status, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);

    LayoutEditor(state);
    RefreshList(state);
    CenterOverOwner(state.window, owner);
    EnableWindow(owner, FALSE);
    ShowWindow(state.window, SW_SHOWNORMAL);
    SetForegroundWindow(state.window);

    MSG message{};
    while (state.window && IsWindow(state.window)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(state.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (state.font) DeleteObject(state.font);
    if (resultMessage) *resultMessage = state.resultMessage;
    return state.changed;
}

} // namespace miaodesk::wallpaper
