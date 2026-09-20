#include "miaodesk/MiaoD3D11TextureLoader.h"

#include "miaodesk/MiaoContentPackage.h"

#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

} // namespace

bool MiaoD3D11TextureLoader::LoadImage(
    ID3D11Device* device,
    const std::filesystem::path& path,
    ID3D11ShaderResourceView** view,
    std::wstring* error) {
    if (!device || !view) return Fail(error, L"D3D11 texture loader parameters are invalid.");
    *view = nullptr;
    if (path.empty() || !std::filesystem::is_regular_file(path))
        return Fail(error, L"Texture image does not exist: " + path.wstring());

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        hr = CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf()));
    }
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
    constexpr UINT kMaxDimension = 16384;
    if (width > kMaxDimension || height > kMaxDimension)
        return Fail(error, L"Package texture exceeds 16384x16384 safety limit.");

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())))
        return Fail(error, L"Cannot create WIC format converter.");
    if (FAILED(converter->Initialize(
            frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom))) {
        return Fail(error, L"Cannot convert package texture to BGRA8.");
    }

    const std::uint64_t rowPitch64 = static_cast<std::uint64_t>(width) * 4ull;
    const std::uint64_t bytes64 = rowPitch64 * static_cast<std::uint64_t>(height);
    if (rowPitch64 > std::numeric_limits<UINT>::max() || bytes64 > 512ull * 1024ull * 1024ull)
        return Fail(error, L"Decoded package texture exceeds memory safety budget.");

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(bytes64));
    if (FAILED(converter->CopyPixels(
            nullptr, static_cast<UINT>(rowPitch64),
            static_cast<UINT>(pixels.size()), pixels.data()))) {
        return Fail(error, L"Cannot copy decoded package texture pixels.");
    }

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels.data();
    initial.SysMemPitch = static_cast<UINT>(rowPitch64);

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&textureDesc, &initial, texture.GetAddressOf())))
        return Fail(error, L"Cannot create D3D11 package texture.");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(texture.Get(), &srvDesc, view)))
        return Fail(error, L"Cannot create D3D11 shader resource view for package texture.");

    if (error) error->clear();
    return true;
}

bool MiaoD3D11TextureLoader::SelfTestPathPolicy() {
    return MiaoContentPackage::IsSafeRelativePath(L"assets/background.png") &&
           !MiaoContentPackage::IsSafeRelativePath(L"../outside.png") &&
           !MiaoContentPackage::IsSafeRelativePath(L"C:\\outside.png");
}

} // namespace miaodesk::content
