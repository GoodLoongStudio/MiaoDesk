#include "miaodesk/MiaoD2DTextureLoader.h"

#include "miaodesk/MiaoContentPackage.h"

#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

// A wallpaper sprite is drawn into a rect no larger than the screen it lands on, so
// anything past the largest panel anyone sells is either an authoring mistake or a
// malformed package. 8192 per side is generous for a full-bleed 8K panel; the byte
// budget is the half that actually protects the process.
//
// Deliberately tighter than MiaoD3D11TextureLoader's 16384 / 512 MiB. That loader's
// destination is IMMUTABLE VRAM uploaded once and only ever read by a shader. A D2D
// bitmap is different in the ways that matter for a bailout policy: D2D re-uploads it
// every time the device is lost, and it has to fit the render target — which in this
// product is sometimes a WIC bitmap render target backed by paged memory. The two
// policies are allowed to differ; what is not allowed is silently guessing why.
constexpr UINT kMaxDimension = 8192;
constexpr std::uint64_t kMaxBytes = 128ull * 1024ull * 1024ull;

} // namespace

// The W suffix is required, not decoration. <windows.h> maps the plain name LoadImage
// onto a LoadImageA/LoadImageW macro, so spelling it without the suffix renames the
// qualified member differently here than in the header — the cross-compile gate reports
// it as "no declaration matches bool MiaoD2DTextureLoader::LoadImageW(...)". The header
// documents the same rule; the two spellings must match exactly.
bool MiaoD2DTextureLoader::LoadImageW(
    ID2D1RenderTarget* target,
    const std::filesystem::path& path,
    ID2D1Bitmap** bitmap,
    std::wstring* error) {
    if (!target || !bitmap) return Fail(error, L"D2D texture loader parameters are invalid.");
    *bitmap = nullptr;
    if (path.empty() || !std::filesystem::is_regular_file(path))
        return Fail(error, L"Texture image does not exist: " + path.wstring());

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return Fail(error, L"Cannot create WIC imaging factory for package texture.");

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
        decoder.GetAddressOf());
    if (FAILED(hr)) return Fail(error, L"Cannot decode package texture: " + path.wstring());

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())))
        return Fail(error, L"Cannot read first frame from package texture.");

    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0)
        return Fail(error, L"Package texture has invalid dimensions.");
    if (width > kMaxDimension || height > kMaxDimension)
        return Fail(error, L"Package texture exceeds the D2D limit of 8192x8192.");

    const std::uint64_t rowPitch64 = static_cast<std::uint64_t>(width) * 4ull;
    const std::uint64_t bytes64 = rowPitch64 * static_cast<std::uint64_t>(height);
    if (rowPitch64 > std::numeric_limits<UINT>::max() || bytes64 > kMaxBytes)
        return Fail(error, L"Decoded package texture exceeds the D2D memory budget.");

    // PBGRA (premultiplied), not BGRA. CreateBitmapFromWicBitmap derives the bitmap's
    // D2D1_ALPHA_MODE from this GUID, and premultiplied is the mode the D2D pipeline
    // wants natively; feeding it straight BGRA instead makes every blend slightly
    // wrong in a way that only shows up on translucent edges.
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())))
        return Fail(error, L"Cannot create WIC format converter.");
    if (FAILED(converter->Initialize(
            frame.Get(), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom))) {
        return Fail(error, L"Cannot convert package texture to premultiplied BGRA.");
    }

    // nullptr properties: default 96 dpi, no alpha-mode override, so the bitmap only
    // depends on the package image and not on a DPI the host happens to be running at.
    hr = target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, bitmap);
    if (FAILED(hr) || !*bitmap)
        return Fail(error, L"Cannot create D2D bitmap from package texture: " + path.wstring());

    if (error) error->clear();
    return true;
}

bool MiaoD2DTextureLoader::SelfTestPathPolicy() {
    return MiaoContentPackage::IsSafeRelativePath(L"assets/photo.png") &&
           !MiaoContentPackage::IsSafeRelativePath(L"../outside.png") &&
           !MiaoContentPackage::IsSafeRelativePath(L"C:\\outside.png");
}

} // namespace miaodesk::content
