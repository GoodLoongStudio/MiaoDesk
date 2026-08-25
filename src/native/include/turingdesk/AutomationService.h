#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "turingdesk/WallpaperAutomation.h"

namespace turingdesk::desktop {

struct AutomationServiceResult {
    bool success{};
    std::wstring message;
};

struct AutomationState {
    bool enabled{true};
    std::wstring activePlaylistId;
    std::wstring lastMatchedScheduleId;
    std::vector<wallpaper::WallpaperProfile> profiles;
    std::vector<wallpaper::WallpaperPlaylist> playlists;
    std::vector<wallpaper::WallpaperSchedule> schedules;
};

// Domain boundary for playlists, schedules and desktop profiles. UI windows
// should not own WallpaperAutomationStore persistence or evaluation state.
class AutomationService {
public:
    AutomationService() = default;

    AutomationServiceResult GetState(AutomationState* state) const;
    AutomationServiceResult SetEnabled(bool enabled) const;
    AutomationServiceResult SetActivePlaylist(std::wstring playlistId) const;

    AutomationServiceResult UpsertProfile(wallpaper::WallpaperProfile profile) const;
    AutomationServiceResult UpsertPlaylist(wallpaper::WallpaperPlaylist playlist) const;
    AutomationServiceResult UpsertSchedule(wallpaper::WallpaperSchedule schedule) const;

    AutomationServiceResult RemoveProfile(std::wstring_view id) const;
    AutomationServiceResult RemovePlaylist(std::wstring_view id) const;
    AutomationServiceResult RemoveSchedule(std::wstring_view id) const;

    AutomationServiceResult ForceNextPlaylist(
        std::wstring_view playlistId,
        unsigned long long unixSeconds,
        wallpaper::AutomationDecision* decision) const;

    static std::wstring MakeId(std::wstring_view prefix);
};

} // namespace turingdesk::desktop
