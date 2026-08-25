#include "turingdesk/AutomationUiAdapter.h"

#include <algorithm>
#include <utility>

namespace turingdesk::wallpaper {

bool AutomationUiAdapter::AssignError(const desktop::AutomationServiceResult& result, std::wstring* error) {
    if (result.success) return true;
    if (error) *error = result.message;
    return false;
}

bool AutomationUiAdapter::Refresh(std::wstring* error) {
    desktop::AutomationState next;
    const auto result = service_.GetState(&next);
    if (!AssignError(result, error)) return false;
    state_ = std::move(next);
    return true;
}

bool AutomationUiAdapter::Load(std::wstring* error) { return Refresh(error); }

const std::vector<WallpaperProfile>& AutomationUiAdapter::Profiles() const noexcept { return state_.profiles; }
const std::vector<WallpaperPlaylist>& AutomationUiAdapter::Playlists() const noexcept { return state_.playlists; }
const std::vector<WallpaperSchedule>& AutomationUiAdapter::Schedules() const noexcept { return state_.schedules; }

std::optional<WallpaperProfile> AutomationUiAdapter::FindProfile(std::wstring_view id) const {
    const auto it = std::find_if(state_.profiles.begin(), state_.profiles.end(), [&](const auto& item) { return item.id == id; });
    return it == state_.profiles.end() ? std::nullopt : std::optional<WallpaperProfile>(*it);
}

std::optional<WallpaperPlaylist> AutomationUiAdapter::FindPlaylist(std::wstring_view id) const {
    const auto it = std::find_if(state_.playlists.begin(), state_.playlists.end(), [&](const auto& item) { return item.id == id; });
    return it == state_.playlists.end() ? std::nullopt : std::optional<WallpaperPlaylist>(*it);
}

std::optional<WallpaperSchedule> AutomationUiAdapter::FindSchedule(std::wstring_view id) const {
    const auto it = std::find_if(state_.schedules.begin(), state_.schedules.end(), [&](const auto& item) { return item.id == id; });
    return it == state_.schedules.end() ? std::nullopt : std::optional<WallpaperSchedule>(*it);
}

bool AutomationUiAdapter::UpsertProfile(WallpaperProfile profile, std::wstring* error) {
    const auto result = service_.UpsertProfile(std::move(profile));
    return AssignError(result, error) && Refresh(error);
}

bool AutomationUiAdapter::UpsertPlaylist(WallpaperPlaylist playlist, std::wstring* error) {
    const auto result = service_.UpsertPlaylist(std::move(playlist));
    return AssignError(result, error) && Refresh(error);
}

bool AutomationUiAdapter::UpsertSchedule(WallpaperSchedule schedule, std::wstring* error) {
    const auto result = service_.UpsertSchedule(std::move(schedule));
    return AssignError(result, error) && Refresh(error);
}

bool AutomationUiAdapter::RemoveProfile(std::wstring_view id, std::wstring* error) {
    const auto result = service_.RemoveProfile(id);
    return AssignError(result, error) && Refresh(error);
}

bool AutomationUiAdapter::RemovePlaylist(std::wstring_view id, std::wstring* error) {
    const auto result = service_.RemovePlaylist(id);
    return AssignError(result, error) && Refresh(error);
}

bool AutomationUiAdapter::RemoveSchedule(std::wstring_view id, std::wstring* error) {
    const auto result = service_.RemoveSchedule(id);
    return AssignError(result, error) && Refresh(error);
}

bool AutomationUiAdapter::SetEnabled(bool enabled, std::wstring* error) {
    const auto result = service_.SetEnabled(enabled);
    return AssignError(result, error) && Refresh(error);
}

bool AutomationUiAdapter::Enabled() const noexcept { return state_.enabled; }

bool AutomationUiAdapter::SetActivePlaylist(std::wstring playlistId, std::wstring* error) {
    const auto result = service_.SetActivePlaylist(std::move(playlistId));
    return AssignError(result, error) && Refresh(error);
}

const std::wstring& AutomationUiAdapter::ActivePlaylistId() const noexcept { return state_.activePlaylistId; }

AutomationDecision AutomationUiAdapter::Evaluate(const SYSTEMTIME& localTime, unsigned long long unixSeconds) {
    AutomationDecision decision;
    const auto result = service_.Evaluate(localTime, unixSeconds, &decision);
    if (result.success) Refresh(nullptr);
    return decision;
}

AutomationDecision AutomationUiAdapter::ForceNextPlaylist(std::wstring_view playlistId, unsigned long long unixSeconds) {
    AutomationDecision decision;
    const auto result = service_.ForceNextPlaylist(playlistId, unixSeconds, &decision);
    if (result.success) Refresh(nullptr);
    return decision;
}

const std::wstring& AutomationUiAdapter::LastMatchedScheduleId() const noexcept { return state_.lastMatchedScheduleId; }

bool AutomationUiAdapter::SelfTest() { return desktop::AutomationService::SelfTest(); }

std::wstring AutomationUiAdapter::MakeId(std::wstring_view prefix) { return desktop::AutomationService::MakeId(prefix); }

} // namespace turingdesk::wallpaper
