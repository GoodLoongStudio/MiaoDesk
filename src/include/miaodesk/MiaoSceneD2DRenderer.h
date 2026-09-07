#pragma once

#include <d2d1.h>

#include <filesystem>
#include <memory>
#include <string>

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
    void Reset() noexcept;

    bool Loaded() const noexcept;
    std::wstring PackageId() const;
    std::wstring LastErrorText() const;

    static bool SelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::content
