#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/AutomationService.h"

namespace turingdesk::wallpaper {

// Store-shaped adapter used only by legacy UI/runtime code while it is migrated.
// All persistence, mutations and evaluation are delegated to AutomationService.
class AutomationUiAdapter {
public:
    AutomationUiAdapter() = default;

    bool Load(std::wstring* error = nullptr);

    const std::vector<WallpaperProfile>& Profiles() const noexcept;
    const std::vector<WallpaperPlaylist>& Playlists() const noexcept;
    const std::vector<WallpaperSchedule>& Schedules() const noexcept;

    std::optional<WallpaperProfile> FindProfile(std::wstring_view id) const;
    std::optional<WallpaperPlaylist> FindPlaylist(std::wstring_view id) const;
    std::optional<WallpaperSchedule> FindSchedule(std::wstring_view id) const;

    bool UpsertProfile(WallpaperProfile profile, std::wstring* error = nullptr);
    bool UpsertPlaylist(WallpaperPlaylist playlist, std::wstring* error = nullptr);
    bool UpsertSchedule(WallpaperSchedule schedule, std::wstring* error = nullptr);
    bool RemoveProfile(std::wstring_view id, std::wstring* error = nullptr);
    bool RemovePlaylist(std::wstring_view id, std::wstring* error = nullptr);
    bool RemoveSchedule(std::wstring_view id, std::wstring* error = nullptr);

    bool SetEnabled(bool enabled, std::wstring* error = nullptr);
    bool Enabled() const noexcept;
    bool SetActivePlaylist(std::wstring playlistId, std::wstring* error = nullptr);
    const std::wstring& ActivePlaylistId() const noexcept;

    AutomationDecision Evaluate(const SYSTEMTIME& localTime, unsigned long long unixSeconds);
    AutomationDecision ForceNextPlaylist(std::wstring_view playlistId, unsigned long long unixSeconds);
    const std::wstring& LastMatchedScheduleId() const noexcept;

    static std::wstring MakeId(std::wstring_view prefix);

private:
    bool Refresh(std::wstring* error = nullptr);
    static bool AssignError(const desktop::AutomationServiceResult& result, std::wstring* error);

    desktop::AutomationService service_;
    desktop::AutomationState state_;
};

} // namespace turingdesk::wallpaper
