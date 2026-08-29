#pragma once

#include "turingdesk/NativeWeatherData.h"

#include <windows.h>

#include <memory>

namespace turingdesk::wallpaper {

class NativeWeatherService {
public:
    NativeWeatherService();
    ~NativeWeatherService();

    NativeWeatherService(const NativeWeatherService&) = delete;
    NativeWeatherService& operator=(const NativeWeatherService&) = delete;

    void Start(HWND notifyWindow, UINT notifyMessage);
    void Stop();
    NativeWeatherSnapshot Snapshot() const;

    static bool ReadCachedSnapshot(NativeWeatherSnapshot* snapshot);
    static bool SelfTest() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace turingdesk::wallpaper
