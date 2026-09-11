#pragma once

#include <d2d1.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "miaodesk/MiaoSceneFrameScheduler.h"

namespace miaodesk::content {

class MiaoSceneD2DRenderer {
public:
    MiaoSceneD2DRenderer();
    ~MiaoSceneD2DRenderer();

    MiaoSceneD2DRenderer(const MiaoSceneD2DRenderer&) = delete;
    MiaoSceneD2DRenderer& operator=(const MiaoSceneD2DRenderer&) = delete;

    bool Load(
        const std::filesystem::path& packageRoot,
        ID2D1RenderTarget* target,
        std::wstring* error = nullptr);

    bool Draw(float timeSeconds, const D2D1_SIZE_F& size, std::wstring* error = nullptr);
    bool SetParameter(std::wstring_view id, PropertyValue value, std::wstring* error = nullptr);
    bool SetInput(std::wstring_view id, PropertyValue value, std::wstring* error = nullptr);

    // Canonical host-side scheduling entry point. It advances runtime animation
    // state to timeSeconds before reporting demand, so a Once animation always
    // receives its terminal frame before an idle Widget drops to 0 FPS.
    bool PrepareFrame(
        double timeSeconds,
        MiaoSceneFrameDemand* demand,
        std::uint32_t animationFps = MiaoSceneFrameScheduler::kDefaultAnimationFps,
        std::wstring* error = nullptr);

    void Reset() noexcept;

    bool Loaded() const noexcept;
    RuntimeProfile Profile() const noexcept;
    std::uint64_t RuntimeGeneration() const noexcept;
    std::wstring PackageId() const;
    std::wstring LastErrorText() const;

    static bool SelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::content
