// The platform-free half of the D3D11 render-target and texture-loader rules.
//
// This file exists because of a mistake I made while adding P3-6 step 2. I described
// the four D3D11 self-tests as "needing a real D3D11 device" and copied that reason
// across from the D2D target, which really does need one. It is not true here: read
// the implementations and none of them touches a device. `ValidateDimensions` is
// range arithmetic, `ResolveDimensions` is a scale-and-round, `SelfTest` compares the
// results against expected numbers, and `SelfTestPathPolicy` is three string checks.
//
// They only ran on Windows because the .cpp that held them includes <d3d11.h>. That is
// a different problem with a different remedy, and the wrong reason hid it — it made
// this look like a GPU question when the actual fix is to put the pure rules where
// they can be linked everywhere, which is what this file does.
//
// What moved here, and why each piece has no business needing a GPU:
//   · kMaxRenderTargetDimension          a constant
//   · MiaoD3D11RenderTarget::ValidateDimensions   range checks
//   · MiaoD3D11RenderTargetPool::ResolveDimensions  scale, round, clamp
//   · MiaoD3D11RenderTargetPool::SelfTest           asserts the above against numbers
//
// MiaoD3D11TextureLoader::SelfTestPathPolicy is here too, but split by platform, and the
// split is the interesting part. It looks platform-free and one third of it is not:
// `!IsSafeRelativePath(L"C:\\outside.png")` only holds on Windows, because only there
// is a backslash a path separator — on POSIX that string is a legal relative filename.
// The first attempt to move it asserted all three unconditionally and returned false the
// moment it ran on macOS, which is how this was found at all.
//
// So the traversal case and the accept case (the two that mean the same thing
// everywhere) now run on every machine, and the escape case is the platform's own:
// a drive letter plus backslash on Windows, an absolute POSIX path elsewhere. Neither
// platform loses an assertion; each gains the ones it was not running.
//
// `Fail` is duplicated rather than shared: it is a four-line anonymous-namespace helper
// in the original file, used by fourteen functions there, and hoisting it into a header
// to serve two translation units would be a worse trade than a copy.
//
// Deliberately still in the Windows-only files: everything that actually creates or
// holds a D3D11 object. The split is "the rules" from "the objects".
#include "miaodesk/MiaoD3D11RenderTarget.h"
#include "miaodesk/MiaoD3D11TextureLoader.h"
#include "miaodesk/MiaoContentPackage.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace miaodesk::content {
namespace {

constexpr unsigned kMaxRenderTargetDimension = 16384;

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

} // namespace

bool MiaoD3D11RenderTarget::ValidateDimensions(
    unsigned width, unsigned height, std::wstring* error) {
    if (width == 0 || height == 0)
        return Fail(error, L"D3D11 render target dimensions must be non-zero.");
    if (width > kMaxRenderTargetDimension || height > kMaxRenderTargetDimension)
        return Fail(error, L"D3D11 render target dimensions exceed the v1 safety limit.");
    if (error) error->clear();
    return true;
}

bool MiaoD3D11RenderTargetPool::ResolveDimensions(
    const RenderResourceDefinition& resource,
    unsigned surfaceWidth,
    unsigned surfaceHeight,
    unsigned* width,
    unsigned* height,
    std::wstring* error) {
    if (!width || !height) return Fail(error, L"Render-target dimension output is null.");
    *width = 0;
    *height = 0;
    if (resource.external)
        return Fail(error, L"External render resources are Host-owned and have no pool dimensions.");
    if (resource.format != RenderResourceFormat::Bgra8Unorm)
        return Fail(error, L"D3D11 render-target pool only supports BGRA8 UNORM in v1.");
    if (resource.sizePolicy != RenderResourceSizePolicy::SurfaceRelative)
        return Fail(error, L"D3D11 render-target pool only supports surface-relative sizing in v1.");
    if (!MiaoD3D11RenderTarget::ValidateDimensions(surfaceWidth, surfaceHeight, error)) return false;
    if (!std::isfinite(resource.widthScale) || !std::isfinite(resource.heightScale) ||
        resource.widthScale <= 0.0f || resource.heightScale <= 0.0f)
        return Fail(error, L"Render-target scale must be finite and positive.");

    const auto scaledWidth = std::max<long long>(
        1, std::llround(static_cast<double>(surfaceWidth) * resource.widthScale));
    const auto scaledHeight = std::max<long long>(
        1, std::llround(static_cast<double>(surfaceHeight) * resource.heightScale));
    if (scaledWidth > kMaxRenderTargetDimension || scaledHeight > kMaxRenderTargetDimension)
        return Fail(error, L"Scaled render-target dimensions exceed the v1 safety limit.");

    *width = static_cast<unsigned>(scaledWidth);
    *height = static_cast<unsigned>(scaledHeight);
    if (error) error->clear();
    return true;
}

bool MiaoD3D11RenderTargetPool::SelfTest() {
    RenderResourceDefinition half;
    half.id = L"renderres://half";
    half.widthScale = 0.5f;
    half.heightScale = 0.25f;
    half.shaderResource = true;

    unsigned width{};
    unsigned height{};
    std::wstring error;
    if (!ResolveDimensions(half, 1920, 1080, &width, &height, &error)) return false;
    if (width != 960 || height != 270) return false;

    RenderResourceDefinition external = half;
    external.external = true;
    if (ResolveDimensions(external, 1920, 1080, &width, &height, &error)) return false;

    RenderResourceDefinition oversized = half;
    oversized.widthScale = 4.0f;
    oversized.heightScale = 4.0f;
    if (ResolveDimensions(oversized, 16384, 16384, &width, &height, &error)) return false;

    // The lower clamp. This assertion is here because of an injection test, not because
    // it was designed in: changing the `std::max(1, ...)` to `std::max(0, ...)` left every
    // existing assertion green, which meant nothing anywhere pinned the "a render target
    // is never zero pixels" rule. A tiny positive scale on a one-pixel surface rounds to
    // 0 and has to come back as 1.
    RenderResourceDefinition tiny = half;
    tiny.widthScale = 0.0001f;
    tiny.heightScale = 0.0001f;
    if (!ResolveDimensions(tiny, 1, 1, &width, &height, &error)) return false;
    if (width != 1 || height != 1) return false;

    return true;
}

// Which image paths a content package may name. Security-relevant: the caller is
// resolving a path that came out of package content, so a pass here is what keeps a
// wallpaper from reading outside its own directory.
bool MiaoD3D11TextureLoader::SelfTestPathPolicy() {
    // The two assertions that hold everywhere: an in-package asset is fine, and a
    // parent-directory escape is not.
    if (!MiaoContentPackage::IsSafeRelativePath(L"assets/background.png")) return false;
    if (MiaoContentPackage::IsSafeRelativePath(L"../outside.png")) return false;

#ifdef _WIN32
    // On Windows a drive letter plus a backslash is an absolute path, so it has to be
    // refused. This is the assertion that made the whole self-test Windows-only before
    // it was split.
    if (MiaoContentPackage::IsSafeRelativePath(L"C:\\outside.png")) return false;
#else
    // The equivalent escape for this platform: a rooted path. On POSIX there is no drive
    // letter, so that case does not apply — and asserting it here would be asserting
    // something false.
    if (MiaoContentPackage::IsSafeRelativePath(L"/etc/passwd")) return false;
#endif
    return true;
}

} // namespace miaodesk::content
