#pragma once

#include <array>
#include <string>

namespace miaodesk::wallpaper {

struct NativeWeatherHour {
    std::wstring label;
    int temperatureC{};
    int weatherCode{};
};

struct NativeWeatherSnapshot {
    bool valid{};
    std::wstring location;
    double latitude{};
    double longitude{};
    int temperatureC{};
    int highC{};
    int lowC{};
    int weatherCode{};
    std::wstring condition;
    std::array<NativeWeatherHour, 4> hours{};
    std::wstring observedTime;
    std::wstring status;
};

} // namespace miaodesk::wallpaper
