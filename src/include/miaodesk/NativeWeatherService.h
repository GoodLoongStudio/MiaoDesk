#pragma once

#include "miaodesk/NativeWeatherData.h"

#include <windows.h>

#include <memory>

namespace miaodesk::wallpaper {

// Network I/O and provider-specific parsing stay outside the Direct2D painter.
// Consumers read an immutable snapshot so widget painting remains cheap and deterministic.
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

} // namespace miaodesk::wallpaper
