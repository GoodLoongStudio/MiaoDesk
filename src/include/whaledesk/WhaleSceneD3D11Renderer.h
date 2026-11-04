#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct HWND__;
using HWND = HWND__*;

namespace whaledesk::content {

// Forward declaration rather than an include: this header deliberately keeps the
// D3D headers out so a consumer does not pull d3d11.h through the renderer.
class WhaleSceneRuntime;

class WhaleSceneD3D11Renderer {
public:
    WhaleSceneD3D11Renderer();
    ~WhaleSceneD3D11Renderer();

    WhaleSceneD3D11Renderer(const WhaleSceneD3D11Renderer&) = delete;
    WhaleSceneD3D11Renderer& operator=(const WhaleSceneD3D11Renderer&) = delete;

    bool Load(const std::filesystem::path& packageRoot, HWND window, std::wstring* error = nullptr);
    bool Draw(float timeSeconds, std::wstring* error = nullptr);
    bool Resize(unsigned width, unsigned height, std::wstring* error = nullptr);
    void Reset() noexcept;

    bool Loaded() const noexcept;

    // The scene runtime this renderer owns, or nullptr when nothing is loaded. See
    // WhaleSceneD2DRenderer::Runtime for why a host needs it.
    WhaleSceneRuntime* Runtime() noexcept;
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
    // Reads the render graph's final colour target, not the swap chain. With no particle
    // or post-process pass this is scene-color; otherwise it is particle-color or the
    // final post-process output. The back buffer is undefined once Draw() has Presented,
    // so reading it would make execution-level tests driver-dependent.
    //
    // Returns false with a message when the renderer is not loaded, when the render graph
    // has no final colour target, or when that target is not a 32-bit RGBA format — never
    // by guessing a pixel size.
    bool ReadBackPixels(std::vector<unsigned char>* bgra, unsigned* width, unsigned* height,
                        std::wstring* error = nullptr);

    static bool SelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace whaledesk::content
