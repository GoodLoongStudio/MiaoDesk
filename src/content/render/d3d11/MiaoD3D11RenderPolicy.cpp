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
// MiaoD3D11TextureLoader::SelfTestPathPolicy is deliberately NOT here, and the reason
// is worth writing down because I assumed otherwise: it looks platform-free and is not.
// One of its three assertions is `!IsSafeRelativePath(L"C:\\outside.png")` — a drive
// letter plus a backslash only escapes a package on Windows, because only there is a
// backslash a path separator. On POSIX that string is a single relative filename, the
// assertion is false, and the whole self-test returns false. It ran green on Windows CI
// for that reason and would have gone red on every other machine the day it moved.
// Running it here is what found that; see docs/TODO.md.
//
// `Fail` is duplicated rather than shared: it is a four-line anonymous-namespace helper
// in the original file, used by fourteen functions there, and hoisting it into a header
// to serve two translation units would be a worse trade than a copy.
//
// Deliberately still in the Windows-only files: everything that actually creates or
// holds a D3D11 object. The split is "the rules" from "the objects".
#include "miaodesk/MiaoD3D11RenderTarget.h"

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

} // namespace miaodesk::content
