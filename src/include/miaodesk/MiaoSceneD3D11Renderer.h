#pragma once

#include <filesystem>
#include <memory>
#include <string>

struct HWND__;
using HWND = HWND__*;

namespace miaodesk::content {

class MiaoSceneD3D11Renderer {
public:
    MiaoSceneD3D11Renderer();
    ~MiaoSceneD3D11Renderer();

    MiaoSceneD3D11Renderer(const MiaoSceneD3D11Renderer&) = delete;
    MiaoSceneD3D11Renderer& operator=(const MiaoSceneD3D11Renderer&) = delete;

    bool Load(const std::filesystem::path& packageRoot, HWND window, std::wstring* error = nullptr);
    bool Draw(float timeSeconds, std::wstring* error = nullptr);
    bool Resize(unsigned width, unsigned height, std::wstring* error = nullptr);
    void Reset() noexcept;

    bool Loaded() const noexcept;
    bool UsesProgrammableMaterial() const noexcept;
    std::wstring PackageId() const;
    std::wstring LastErrorText() const;

    static bool SelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::content
