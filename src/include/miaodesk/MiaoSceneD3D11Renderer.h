#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct HWND__;
using HWND = HWND__*;

namespace miaodesk::content {

// Forward declaration rather than an include: this header deliberately keeps the
// D3D headers out so a consumer does not pull d3d11.h through the renderer.
class MiaoSceneRuntime;

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

    // The scene runtime this renderer owns, or nullptr when nothing is loaded. See
    // MiaoSceneD2DRenderer::Runtime for why a host needs it.
    MiaoSceneRuntime* Runtime() noexcept;
    bool UsesProgrammableMaterial() const noexcept;
    std::wstring PackageId() const;
    std::wstring LastErrorText() const;

    // The last drawn frame as tightly packed BGRA, or nothing when nothing was drawn.
    //
    // Same justification as Runtime(): this is something the host needs that lives inside
    // the renderer. It is also what turns "the textured sprite path draws" from a
    // compile-and-link statement into a checkable one — before it existed, the D3D11
    // renderer could not report what it had drawn, so no test could assert it.
    //
    // Reads the scene colour target, not the swap chain: that is what the scene pass drew
    // into, and the back buffer is undefined once Draw() has Presented.
    //
    // Returns false with a message when the renderer is not loaded, when the render graph
    // has no scene colour target, or when that target is not a 32-bit RGBA format — never
    // by guessing a pixel size.
    bool ReadBackPixels(std::vector<unsigned char>* bgra, unsigned* width, unsigned* height,
                        std::wstring* error = nullptr);

    static bool SelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::content
