from pathlib import Path


def load(path):
    return Path(path).read_text(encoding='utf-8')


def save(path, text):
    Path(path).write_text(text, encoding='utf-8', newline='\n')


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


def section(text, start, end, replacement, label):
    i = text.find(start)
    if i < 0:
        raise SystemExit(f'{label}: start anchor missing')
    j = text.find(end, i + len(start))
    if j < 0:
        raise SystemExit(f'{label}: end anchor missing')
    return text[:i] + replacement + text[j:]

# 1) Make the Desktop Library itself own all three page states.
v2_path = 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2.cpp'
v2 = load(v2_path)
v2 = one(v2,
    '#include "turingdesk/WallpaperLibraryWindow.h"\n#include "turingdesk/DesktopWidgetController.h"',
    '#include "turingdesk/WallpaperLibraryWindow.h"\n#include "turingdesk/DesktopAiSettingsPage.h"\n#include "turingdesk/DesktopWidgetController.h"',
    'v2 include')
v2 = one(v2, '    enum class Page { Installed, Widgets };',
             '    enum class Page { Installed, Widgets, AI };', 'v2 page enum')

new_footer = r'''    void UpdateFooter() {
        const bool installed = page == Page::Installed;
        const bool widgets = page == Page::Widgets;
        const bool showFooter = installed || widgets;
        ShowWindow(status, showFooter ? SW_SHOW : SW_HIDE);
        ShowWindow(targetCombo, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(applyButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(favoriteButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(removeButton, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetCreateButton, widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetToggleButton, widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetRemoveButton, widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetRefreshButton, widgets ? SW_SHOW : SW_HIDE);

        if (installed) {
            const auto selected = SelectedWallpaper();
            const bool usable = selected && !SourceMissing(*selected);
            EnableWindow(applyButton, usable ? TRUE : FALSE);
            EnableWindow(favoriteButton, selected ? TRUE : FALSE);
            EnableWindow(removeButton, selected && selected->kind != LibraryWallpaperKind::Scene ? TRUE : FALSE);
            if (!selected) SetStatus(L"选择一个桌面；双击卡片可直接应用。");
            else {
                std::wstring text = selected->title + L" · " + KindLabel(selected->kind);
                if (SourceMissing(*selected)) text += L" · 资源不可用";
                SetStatus(std::move(text));
                SetWindowTextW(favoriteButton, selected->favorite ? L"取消收藏" : L"收藏");
            }
        } else if (widgets) {
            const auto widget = SelectedWidget();
            EnableWindow(widgetToggleButton, widget ? TRUE : FALSE);
            EnableWindow(widgetRemoveButton, widget ? TRUE : FALSE);
            if (!widget) {
                SetStatus(L"小组件可直接在桌面拖动；位置会自动保存。");
            } else {
                const auto* health = HealthFor(widget->id);
                std::wostringstream text;
                text << widget->title << L" · " << (widget->enabled ? L"已启用" : L"已停用")
                     << L" · " << FriendlyMonitor(widget->monitorId);
                if (widget->enabled && health) {
                    if (health->renderingHealthy) text << L" · 运行正常";
                    else if (!health->detail.empty()) text << L" · " << health->detail;
                    if (!health->recommendedAction.empty()) text << L" · " << health->recommendedAction;
                }
                SetStatus(text.str());
                SetWindowTextW(widgetToggleButton, widget->enabled ? L"停用" : L"启用");
            }
        }
        Layout();
    }

'''
v2 = section(v2, '    void UpdateFooter() {', '    void SetPage(Page next) {', new_footer, 'v2 footer')

new_set_page = r'''    void SetPage(Page next) {
        page = next;
        const bool installed = page == Page::Installed;
        const bool widgets = page == Page::Widgets;
        const bool ai = page == Page::AI;
        activeNavId = installed ? kNavInstalledId : widgets ? kNavWidgetsId : kNavAiId;
        SetWindowTextW(sectionTitle, installed ? L"壁纸库" : widgets ? L"小组件" : L"妙喵 AI");
        ShowWindow(wallpaperGrid, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(widgetGrid, widgets ? SW_SHOW : SW_HIDE);
        ShowWindow(search, installed ? SW_SHOW : SW_HIDE);
        ShowWindow(addButton, installed ? SW_SHOW : SW_HIDE);
        if (!installed && webBarVisible) HideWebBar();
        if (ai) ShowDesktopAiSettingsPage(window);
        else HideDesktopAiSettingsPage(window);
        UpdateFooter();
        InvalidateRect(window, nullptr, FALSE);
        for (HWND button : nav) InvalidateRect(button, nullptr, FALSE);
    }

'''
v2 = section(v2, '    void SetPage(Page next) {', '    void ApplySelected() {', new_set_page, 'v2 set page')

new_handle_nav = r'''    void HandleNav(int id) {
        if (id == kNavInstalledId) { SetPage(Page::Installed); return; }
        if (id == kNavWidgetsId) { RefreshWidgets(); SetPage(Page::Widgets); return; }
        if (id == kNavAiId) { SetPage(Page::AI); return; }
    }

'''
v2 = section(v2, '    void HandleNav(int id) {', '    void ShowWallpaperContextMenu(POINT screenPoint) {', new_handle_nav, 'v2 nav')

new_layout = r'''    void Layout() {
        if (!window) return;
        RECT rc{};
        GetClientRect(window, &rc);
        const int width = RectWidth(rc);
        const int height = RectHeight(rc);
        const int sidebarW = S(208);
        const int topH = S(58);
        const int footerH = S(58);
        const int margin = std::clamp((width - sidebarW) / 36, S(14), S(20));
        const bool installed = page == Page::Installed;
        const bool widgets = page == Page::Widgets;
        const int webH = webBarVisible && installed ? S(48) : 0;

        auto place = [&](HWND child, int x, int y, int w, int h) {
            if (!child) return;
            SetWindowPos(child, nullptr, x, y, std::max(1, w), std::max(1, h),
                         SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
        };

        place(title, S(18), S(16), sidebarW - S(36), S(28));
        int navY = topH + S(18);
        for (HWND button : nav) {
            place(button, S(12), navY, sidebarW - S(24), S(38));
            navY += S(42);
        }

        const int contentLeft = sidebarW;
        const int contentWidth = std::max(1, width - contentLeft);
        const int headerLeft = contentLeft + margin;
        const int headerRight = width - margin;
        if (installed) {
            const int addW = S(96);
            const int titleW = std::clamp(contentWidth * 21 / 100, S(112), S(190));
            const int titleRight = headerLeft + titleW;
            const int addLeft = headerRight - addW;
            const int searchLeft = titleRight + S(12);
            const int searchW = std::max(S(120), addLeft - S(12) - searchLeft);
            place(sectionTitle, headerLeft, S(17), titleW, S(30));
            place(search, searchLeft, S(12), searchW, S(34));
            place(addButton, addLeft, S(11), addW, S(36));
        } else {
            place(sectionTitle, headerLeft, S(17), std::max(S(160), contentWidth - margin * 2), S(30));
        }

        if (webBarVisible && installed) {
            const int webTop = topH;
            const int buttonsW = S(202);
            const int urlW = std::max(S(160), contentWidth - margin * 2 - buttonsW - S(12));
            place(webUrl, contentLeft + margin, webTop + S(7), urlW, S(34));
            place(webConfirm, width - margin - S(202), webTop + S(7), S(96), S(34));
            place(webCancel, width - margin - S(98), webTop + S(7), S(98), S(34));
        }

        const int contentTop = topH + webH;
        const int contentBottom = std::max(contentTop, height - footerH);
        const int contentH = std::max(1, contentBottom - contentTop);
        place(wallpaperGrid, contentLeft, contentTop, contentWidth, contentH);
        place(widgetGrid, contentLeft, contentTop, contentWidth, contentH);
        UpdateGridScroll(wallpaperGrid, false);
        UpdateGridScroll(widgetGrid, true);

        const int footerTop = height - footerH;
        if (installed) {
            const int gap = S(6);
            const int actionW = std::clamp(contentWidth * 15 / 100, S(82), S(108));
            const int smallW = std::clamp(contentWidth * 11 / 100, S(68), S(88));
            const int targetW = std::clamp(contentWidth * 23 / 100, S(118), S(182));
            const int right = width - margin;
            const int actionTotal = targetW + actionW + smallW * 2 + gap * 3;
            const int actionsLeft = right - actionTotal;
            const int statusLeft = contentLeft + margin;
            const int statusW = std::max(S(90), actionsLeft - S(10) - statusLeft);
            place(status, statusLeft, footerTop + S(18), statusW, S(26));
            int x = actionsLeft;
            place(targetCombo, x, footerTop + S(11), targetW, S(180)); x += targetW + gap;
            place(favoriteButton, x, footerTop + S(11), smallW, S(36)); x += smallW + gap;
            place(removeButton, x, footerTop + S(11), smallW, S(36)); x += smallW + gap;
            place(applyButton, x, footerTop + S(11), actionW, S(36));
        } else if (widgets) {
            const int gap = S(6);
            const int buttonW = std::clamp(contentWidth * 11 / 100, S(70), S(84));
            const int createW = std::clamp(contentWidth * 20 / 100, S(124), S(154));
            const int right = width - margin;
            const int actionTotal = createW + buttonW * 3 + gap * 3;
            const int actionsLeft = right - actionTotal;
            const int statusLeft = contentLeft + margin;
            const int statusW = std::max(S(90), actionsLeft - S(10) - statusLeft);
            place(status, statusLeft, footerTop + S(18), statusW, S(26));
            int x = actionsLeft;
            place(widgetCreateButton, x, footerTop + S(11), createW, S(36)); x += createW + gap;
            place(widgetRefreshButton, x, footerTop + S(11), buttonW, S(36)); x += buttonW + gap;
            place(widgetToggleButton, x, footerTop + S(11), buttonW, S(36)); x += buttonW + gap;
            place(widgetRemoveButton, x, footerTop + S(11), buttonW, S(36));
        }

        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

'''
v2 = section(v2, '    void Layout() {', '    LRESULT DrawNavButton(const DRAWITEMSTRUCT* draw) {', new_layout, 'v2 layout')
save(v2_path, v2)

# 2) Stop the AI page from hiding/recreating the host layout. It becomes a true content page.
ai_path = 'src/native/src/ui/settings/DesktopAiSettingsPage.cpp'
ai = load(ai_path)
ai = one(ai, '    std::vector<HWND> hiddenHostChildren;\n', '', 'ai hidden vector')
ai = section(ai, '    bool HostNavControl(HWND child) const {', '    int MeasureTextHeight(HWND control, int width, HFONT font, int minimum) const {', '', 'ai hide host helpers')
ai = one(ai,
'''        const int sidebar = S(208);\n        const int parentWidth = std::max(1, static_cast<int>(client.right - client.left));\n        const int parentHeight = std::max(1, static_cast<int>(client.bottom - client.top));\n        SetWindowPos(panel, nullptr, sidebar, 0, std::max(1, parentWidth - sidebar), parentHeight,\n                     SWP_NOACTIVATE | SWP_NOZORDER);''',
'''        const int sidebar = S(208);\n        const int top = S(58);\n        const int footer = S(58);\n        const int parentWidth = std::max(1, static_cast<int>(client.right - client.left));\n        const int parentHeight = std::max(1, static_cast<int>(client.bottom - client.top));\n        SetWindowPos(panel, nullptr, sidebar, top, std::max(1, parentWidth - sidebar),\n                     std::max(1, parentHeight - top - footer),\n                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);''',
'ai host rect')
ai = one(ai,
'''        int y = S(16);\n        const int titleY = y; const int titleH = S(34); y += titleH + S(5);\n        const int introY = y; y += introH + S(12);''',
'''        ShowWindow(title, SW_HIDE);\n        int y = S(16);\n        const int titleY = y; const int titleH = 1;\n        const int introY = y; y += introH + S(12);''',
'ai duplicate title')
ai = one(ai,
'''        if (state->panel && IsWindowVisible(state->panel)) {\n            state->HideHostContent();\n            SetWindowPos(state->panel, HWND_TOP, 0, 0, 0, 0,\n                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);\n        }''',
'''        if (state->panel && IsWindowVisible(state->panel)) {\n            SetWindowPos(state->panel, HWND_TOP, 0, 0, 0, 0,\n                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);\n        }''',
'ai parent resize')
ai = one(ai, '    state->HideHostContent();\n    state->Layout();', '    state->Layout();', 'ai show hide host')
ai = one(ai,
'''        ShowWindow(state->panel, SW_HIDE);\n        state->RestoreHostContent();''',
'''        ShowWindow(state->panel, SW_HIDE);''',
'ai hide restore')
save(ai_path, ai)

# 3) Make the entire Widget surface draggable, not only the 28px strip.
widget_surface_path = 'src/native/src/desktop/wallpaper/web/WebDesktopSurfaceChild.cpp'
ws = load(widget_surface_path)
ws = section(ws, '    void ResizeWidgetDragHandle() {', '    std::wstring WidgetId() const {', r'''    void ResizeWidgetDragHandle() {
        if (!dragHandle_ || !IsWindow(dragHandle_) || !hwnd_) return;
        RECT client{};
        if (!GetClientRect(hwnd_, &client)) return;
        SetWindowPos(dragHandle_, HWND_TOP, 0, 0,
                     std::max<LONG>(1, client.right - client.left),
                     std::max<LONG>(1, client.bottom - client.top),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(dragHandle_, nullptr, FALSE);
    }

''', 'widget full drag overlay')
save(widget_surface_path, ws)

# 4) Add more depth/motion to the default Aurora scene.
wall_path = 'src/native/src/desktop/wallpaper/legacy/WallpaperEngine.cpp'
wall = load(wall_path)
wall = one(wall,
'''        for (int i = 0; i < 46; ++i) {\n            const float x = size.width * static_cast<float>((i * 37 + 11) % 101) / 100.0f;''',
'''        const float haloPulse = 0.5f + 0.5f * static_cast<float>(std::sin(time_ * 0.48f));\n        const D2D1_POINT_2F haloCenter = D2D1::Point2F(\n            size.width * (0.50f + 0.08f * static_cast<float>(std::sin(time_ * 0.19f))),\n            size.height * (0.48f + 0.06f * static_cast<float>(std::cos(time_ * 0.17f))));\n        for (int ring = 0; ring < 4; ++ring) {\n            const float ringScale = 1.0f + ring * 0.22f + haloPulse * 0.08f;\n            brush_->SetColor(D2D1::ColorF(0.28f + ring * 0.06f, 0.62f, 1.0f, 0.10f - ring * 0.014f));\n            renderTarget_->DrawEllipse(\n                D2D1::Ellipse(haloCenter, size.width * 0.12f * ringScale, size.height * 0.20f * ringScale),\n                brush_.Get(), 1.2f + ring * 0.35f);\n        }\n\n        for (int i = 0; i < 18; ++i) {\n            const float progress = static_cast<float>(std::fmod(time_ * (0.020f + (i % 4) * 0.004f) + i * 0.071f, 1.0f));\n            const float x = size.width * (1.10f - progress * 1.25f);\n            const float y = size.height * (0.08f + static_cast<float>((i * 29) % 83) / 100.0f);\n            const float length = size.width * (0.018f + (i % 3) * 0.006f);\n            brush_->SetColor(D2D1::ColorF(0.72f, 0.92f, 1.0f, 0.10f + (i % 4) * 0.035f));\n            renderTarget_->DrawLine(D2D1::Point2F(x, y), D2D1::Point2F(x + length, y - length * 0.16f), brush_.Get(), 1.0f);\n        }\n\n        for (int i = 0; i < 72; ++i) {\n            const float x = size.width * static_cast<float>((i * 37 + 11) % 101) / 100.0f;''',
'aurora depth')
wall = one(wall, '        for (int i = 0; i < 46; ++i) {', '        for (int i = 0; i < 72; ++i) {', 'aurora stars count') if '        for (int i = 0; i < 46; ++i) {' in wall else wall
save(wall_path, wall)

# 5) Push the default Glass Clock further toward a luminous holographic card.
controller_path = 'src/native/src/desktop/widgets/DesktopWidgetController.cpp'
ctl = load(controller_path)
ctl = one(ctl,
'''@keyframes drift{0%{transform:translate(-3%,-2%) rotate(-4deg) scale(1)}100%{transform:translate(4%,3%) rotate(5deg) scale(1.08)}}''',
'''.card:after{content:"";position:absolute;left:-38%;top:-55%;width:42%;height:220%;background:linear-gradient(90deg,transparent,rgba(255,255,255,.14),rgba(121,240,255,.20),transparent);transform:rotate(19deg);mix-blend-mode:screen;animation:sweep 7.5s ease-in-out infinite}.chip:before{content:"";display:inline-block;width:6px;height:6px;margin-right:6px;border-radius:50%;background:#64ffe0;box-shadow:0 0 12px #64ffe0;vertical-align:1px}.mesh{animation:meshFloat 8s ease-in-out infinite alternate}.time{filter:drop-shadow(0 0 18px rgba(118,215,255,.24))}\n@keyframes drift{0%{transform:translate(-3%,-2%) rotate(-4deg) scale(1)}100%{transform:translate(4%,3%) rotate(5deg) scale(1.08)}}@keyframes sweep{0%,18%{transform:translateX(-20%) rotate(19deg);opacity:0}45%{opacity:1}72%,100%{transform:translateX(360%) rotate(19deg);opacity:0}}@keyframes meshFloat{to{transform:translate3d(10px,7px,0)}}''',
'glass clock effects')
save(controller_path, ctl)

print('adaptive shell + full widget drag + visual polish applied')
