#include "turingdesk/WidgetIntentComposer.h"

#include "turingdesk/A2UIParser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <sstream>
#include <string>

namespace turingdesk::widget_intent {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool ContainsAny(const std::wstring& haystack, std::initializer_list<const wchar_t*> needles) {
    for (const wchar_t* needle : needles) {
        if (haystack.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

a2ui::WidgetPlacement PlacementFromPrompt(const std::wstring& lower) {
    a2ui::WidgetPlacement placement;
    if (ContainsAny(lower, {L"左上", L"左上角", L"top left", L"top-left"})) {
        placement = {0.04f, 0.05f, 0.28f, 0.20f};
    } else if (ContainsAny(lower, {L"左下", L"左下角", L"bottom left", L"bottom-left"})) {
        placement = {0.04f, 0.70f, 0.28f, 0.20f};
    } else if (ContainsAny(lower, {L"右下", L"右下角", L"bottom right", L"bottom-right"})) {
        placement = {0.68f, 0.70f, 0.28f, 0.20f};
    } else if (ContainsAny(lower, {L"中间", L"居中", L"中央", L"center", L"centre"})) {
        placement = {0.36f, 0.38f, 0.28f, 0.22f};
    } else if (ContainsAny(lower, {L"右上", L"右上角", L"top right", L"top-right"})) {
        placement = {0.68f, 0.05f, 0.28f, 0.20f};
    } else {
        placement = {0.68f, 0.05f, 0.28f, 0.20f};
    }
    return placement;
}

std::string LayoutJson(const a2ui::WidgetPlacement& placement) {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(2);
    out << "{\"x\":" << placement.x << ",\"y\":" << placement.y << ",\"width\":" << placement.width
        << ",\"height\":" << placement.height << "}";
    return out.str();
}

enum class WidgetKind { TodayTasks, WeatherGlass, FocusClock, SystemPulse, GlassClock };

WidgetKind KindFromPrompt(const std::wstring& lower) {
    if (ContainsAny(lower, {L"待办", L"任务", L"todo", L"清单", L"checklist"})) return WidgetKind::TodayTasks;
    if (ContainsAny(lower, {L"天气", L"weather", L"温度", L"预报"})) return WidgetKind::WeatherGlass;
    if (ContainsAny(lower, {L"专注", L"番茄", L"focus", L"pomodoro"})) return WidgetKind::FocusClock;
    if (ContainsAny(lower, {L"系统", L"cpu", L"内存", L"性能", L"network", L"网络"})) return WidgetKind::SystemPulse;
    if (ContainsAny(lower, {L"时钟", L"钟", L"clock", L"时间"})) return WidgetKind::GlassClock;
    return WidgetKind::TodayTasks;
}

std::wstring TitleForKind(WidgetKind kind) {
    switch (kind) {
    case WidgetKind::WeatherGlass: return L"玻璃天气";
    case WidgetKind::FocusClock: return L"专注时钟";
    case WidgetKind::SystemPulse: return L"系统状态";
    case WidgetKind::GlassClock: return L"玻璃时钟";
    case WidgetKind::TodayTasks:
    default: return L"今日待办";
    }
}

std::string DocumentForKind(WidgetKind kind, const a2ui::WidgetPlacement& placement) {
    const std::string layout = LayoutJson(placement);
    switch (kind) {
    case WidgetKind::WeatherGlass:
        return std::string(R"JSON({"version":1,"type":"Card","props":{"title":"玻璃天气","background":"#BFFFFFFF","foreground":"#24324A","cornerRadius":20,"padding":16,"children":[{"type":"Weather","props":{"location":"本地","unit":"celsius","showForecast":true,"foreground":"#24324A"},"layout":{"x":0.06,"y":0.24,"width":0.88,"height":0.68}}]},"layout":)JSON") +
               layout + "}";
    case WidgetKind::FocusClock:
        return std::string(R"JSON({"version":1,"type":"Card","props":{"title":"专注时钟","subtitle":"25:00","background":"#CC101827","foreground":"#FFFFFF","cornerRadius":22,"padding":18,"children":[{"type":"Text","props":{"text":"保持专注","foreground":"#BFD7FF","fontSize":14},"layout":{"x":0.08,"y":0.62,"width":0.84,"height":0.20}}]},"layout":)JSON") +
               layout + "}";
    case WidgetKind::SystemPulse:
        return std::string(R"JSON({"version":1,"type":"Card","props":{"title":"系统状态","background":"#D91B2230","foreground":"#F8FAFC","cornerRadius":18,"padding":16,"children":[{"type":"List","props":{"items":["CPU --%","Memory --%","Network --"],"foreground":"#DCE7F5","fontSize":14},"layout":{"x":0.06,"y":0.26,"width":0.88,"height":0.64}}]},"layout":)JSON") +
               layout + "}";
    case WidgetKind::GlassClock:
        return std::string(R"JSON({"version":1,"type":"Card","props":{"title":"玻璃时钟","subtitle":"12:00","background":"#CC101827","foreground":"#FFFFFF","cornerRadius":22,"padding":18,"children":[{"type":"Text","props":{"text":"MIAO · DESKTOP","foreground":"#BFD7FF","fontSize":14},"layout":{"x":0.08,"y":0.62,"width":0.84,"height":0.20}}]},"layout":)JSON") +
               layout + "}";
    case WidgetKind::TodayTasks:
    default:
        return std::string(R"JSON({"version":1,"type":"Card","props":{"title":"今日待办","background":"#E6FFFFFF","foreground":"#1F2937","cornerRadius":18,"padding":16,"children":[{"type":"List","props":{"items":["整理桌面","完成预览","提交版本"],"foreground":"#1F2937","fontSize":15},"layout":{"x":0.06,"y":0.24,"width":0.88,"height":0.68}}]},"layout":)JSON") +
               layout + "}";
    }
}

} // namespace

bool LooksLikeOneSentenceWidgetRequest(std::wstring_view prompt) {
    if (prompt.empty()) return false;
    const auto lower = Lower(std::wstring(prompt));

    const bool wallpaperOnly = ContainsAny(lower, {L"壁纸", L"背景", L"wallpaper"}) &&
                               !ContainsAny(lower, {L"小组件", L"组件", L"widget", L"卡片", L"待办", L"天气", L"时钟", L"钟"});
    if (wallpaperOnly) return false;

    const bool widgetNoun = ContainsAny(lower, {
        L"小组件", L"组件", L"widget", L"卡片", L"待办", L"天气", L"时钟", L"钟", L"专注", L"系统",
    });
    const bool widgetVerb = ContainsAny(lower, {
        L"加", L"放", L"来", L"做", L"生成", L"创建", L"帮我", L"想要", L"来个", L"来一个", L"弄", L"安排",
        L"add", L"create", L"make", L"put",
    });
    const bool widgetPlace = ContainsAny(lower, {
        L"右上", L"左上", L"右下", L"左下", L"中间", L"居中", L"中央", L"角落", L"桌面",
    });

    return widgetNoun && (widgetVerb || widgetPlace);
}

ComposedWidget ComposeFromPrompt(std::wstring_view prompt) {
    ComposedWidget result;
    if (!LooksLikeOneSentenceWidgetRequest(prompt)) {
        result.message = L"无法从这句话识别小组件意图。";
        return result;
    }

    const auto lower = Lower(std::wstring(prompt));
    const auto kind = KindFromPrompt(lower);
    const auto placement = PlacementFromPrompt(lower);
    result.title = TitleForKind(kind);
    result.a2uiJson = DocumentForKind(kind, placement);

    const auto validated = a2ui::ValidateWidgetDocument(result.a2uiJson);
    if (!validated.success) {
        result.message = validated.message.empty() ? L"小组件模板校验失败。" : validated.message;
        return result;
    }

    result.a2uiJson = validated.normalizedJson;
    if (!validated.title.empty()) result.title = validated.title;
    result.success = true;
    result.message = L"已根据你的描述生成小组件预览。";
    return result;
}

} // namespace turingdesk::widget_intent
