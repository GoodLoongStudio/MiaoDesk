#include "miaodesk/MiaoD3D11RenderTarget.h"

#include <d3d11.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include <algorithm>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace miaodesk::content {
namespace {

constexpr unsigned kMaxRenderTargetDimension = 16384;

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

} // namespace

struct MiaoD3D11RenderTarget::Impl {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> renderTargetView;
    ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    unsigned width{};
    unsigned height{};
};

MiaoD3D11RenderTarget::MiaoD3D11RenderTarget() : impl_(std::make_unique<Impl>()) {}
MiaoD3D11RenderTarget::~MiaoD3D11RenderTarget() = default;

bool MiaoD3D11RenderTarget::ValidateDimensions(
    unsigned width, unsigned height, std::wstring* error) {
    if (width == 0 || height == 0)
        return Fail(error, L"D3D11 render target dimensions must be non-zero.");
    if (width > kMaxRenderTargetDimension || height > kMaxRenderTargetDimension)
        return Fail(error, L"D3D11 render target dimensions exceed the v1 safety limit.");
    if (error) error->clear();
    return true;
}

bool MiaoD3D11RenderTarget::Create(
    ID3D11Device* device, unsigned width, unsigned height, std::wstring* error) {
    Reset();
    if (!device) return Fail(error, L"D3D11 render target device is null.");
    if (!ValidateDimensions(width, height, error)) return false;

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&textureDesc, nullptr, impl_->texture.GetAddressOf())))
        return Fail(error, L"Cannot create Miao Scene offscreen texture.");
    if (FAILED(device->CreateRenderTargetView(
            impl_->texture.Get(), nullptr, impl_->renderTargetView.GetAddressOf()))) {
        Reset();
        return Fail(error, L"Cannot create Miao Scene offscreen render-target view.");
    }
    if (FAILED(device->CreateShaderResourceView(
            impl_->texture.Get(), nullptr, impl_->shaderResourceView.GetAddressOf()))) {
        Reset();
        return Fail(error, L"Cannot create Miao Scene offscreen shader-resource view.");
    }

    impl_->width = width;
    impl_->height = height;
    if (error) error->clear();
    return true;
}

void MiaoD3D11RenderTarget::Reset() noexcept {
    impl_->shaderResourceView.Reset();
    impl_->renderTargetView.Reset();
    impl_->texture.Reset();
    impl_->width = 0;
    impl_->height = 0;
}

bool MiaoD3D11RenderTarget::Valid() const noexcept {
    return impl_->texture && impl_->renderTargetView && impl_->shaderResourceView;
}

unsigned MiaoD3D11RenderTarget::Width() const noexcept { return impl_->width; }
unsigned MiaoD3D11RenderTarget::Height() const noexcept { return impl_->height; }

ID3D11RenderTargetView* MiaoD3D11RenderTarget::RenderTargetView() const noexcept {
    return impl_->renderTargetView.Get();
}

ID3D11ShaderResourceView* MiaoD3D11RenderTarget::ShaderResourceView() const noexcept {
    return impl_->shaderResourceView.Get();
}

} // namespace miaodesk::content
