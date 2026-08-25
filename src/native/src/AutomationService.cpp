#include "turingdesk/AutomationService.h"

#include <utility>

namespace turingdesk::desktop {
namespace {

AutomationServiceResult LoadStore(wallpaper::WallpaperAutomationStore* store) {
    if (!store) return {false, L"Automation store 不能为空。"};
    std::wstring error;
    if (!store->Load(&error)) return {false, error.empty() ? L"无法读取桌面自动化状态。" : error};
    return {true, L"桌面自动化状态读取完成。"};
}

} // namespace

AutomationServiceResult AutomationService::GetState(AutomationState* state) const {
    if (!state) return {false, L"AutomationState 输出不能为空。"};
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;

    AutomationState next;
    next.enabled = store.Enabled();
    next.activePlaylistId = store.ActivePlaylistId();
    next.lastMatchedScheduleId = store.LastMatchedScheduleId();
    next.profiles = store.Profiles();
    next.playlists = store.Playlists();
    next.schedules = store.Schedules();
    *state = std::move(next);
    return {true, L"桌面自动化状态读取完成。"};
}

AutomationServiceResult AutomationService::SetEnabled(bool enabled) const {
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    std::wstring error;
    if (!store.SetEnabled(enabled, &error))
        return {false, error.empty() ? L"无法更新自动化开关。" : error};
    return {true, enabled ? L"桌面自动化已启用。" : L"桌面自动化已停用。"};
}

AutomationServiceResult AutomationService::SetActivePlaylist(std::wstring playlistId) const {
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    std::wstring error;
    if (!store.SetActivePlaylist(std::move(playlistId), &error))
        return {false, error.empty() ? L"无法更新活动播放列表。" : error};
    return {true, L"活动播放列表已更新。"};
}

AutomationServiceResult AutomationService::UpsertProfile(wallpaper::WallpaperProfile profile) const {
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    std::wstring error;
    if (!store.UpsertProfile(std::move(profile), &error))
        return {false, error.empty() ? L"无法保存桌面配置。" : error};
    return {true, L"桌面配置已保存。"};
}

AutomationServiceResult AutomationService::UpsertPlaylist(wallpaper::WallpaperPlaylist playlist) const {
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    std::wstring error;
    if (!store.UpsertPlaylist(std::move(playlist), &error))
        return {false, error.empty() ? L"无法保存播放列表。" : error};
    return {true, L"播放列表已保存。"};
}

AutomationServiceResult AutomationService::UpsertSchedule(wallpaper::WallpaperSchedule schedule) const {
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    std::wstring error;
    if (!store.UpsertSchedule(std::move(schedule), &error))
        return {false, error.empty() ? L"无法保存定时规则。" : error};
    return {true, L"定时规则已保存。"};
}

AutomationServiceResult AutomationService::RemoveProfile(std::wstring_view id) const {
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    std::wstring error;
    if (!store.RemoveProfile(id, &error)) return {false, error.empty() ? L"无法删除桌面配置。" : error};
    return {true, L"桌面配置已删除。"};
}

AutomationServiceResult AutomationService::RemovePlaylist(std::wstring_view id) const {
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    std::wstring error;
    if (!store.RemovePlaylist(id, &error)) return {false, error.empty() ? L"无法删除播放列表。" : error};
    return {true, L"播放列表已删除。"};
}

AutomationServiceResult AutomationService::RemoveSchedule(std::wstring_view id) const {
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    std::wstring error;
    if (!store.RemoveSchedule(id, &error)) return {false, error.empty() ? L"无法删除定时规则。" : error};
    return {true, L"定时规则已删除。"};
}

AutomationServiceResult AutomationService::ForceNextPlaylist(
    std::wstring_view playlistId,
    unsigned long long unixSeconds,
    wallpaper::AutomationDecision* decision) const {
    if (!decision) return {false, L"AutomationDecision 输出不能为空。"};
    wallpaper::WallpaperAutomationStore store;
    const auto loaded = LoadStore(&store);
    if (!loaded.success) return loaded;
    *decision = store.ForceNextPlaylist(playlistId, unixSeconds);
    return {true, decision->kind == wallpaper::AutomationDecisionKind::None
        ? L"播放列表没有可切换的项目。"
        : L"播放列表已切换到下一项。"};
}

std::wstring AutomationService::MakeId(std::wstring_view prefix) {
    return wallpaper::WallpaperAutomationStore::MakeId(prefix);
}

} // namespace turingdesk::desktop
