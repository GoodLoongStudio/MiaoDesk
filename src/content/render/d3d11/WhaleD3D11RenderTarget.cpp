#include "whaledesk/WhaleD3D11RenderTarget.h"

#include <d3d11.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace whaledesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

} // namespace

struct WhaleD3D11RenderTarget::Impl {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> renderTargetView;
    ComPtr<ID3D11ShaderResourceView> shaderResourceView;
    unsigned width{};
    unsigned height{};
};

WhaleD3D11RenderTarget::WhaleD3D11RenderTarget() : impl_(std::make_unique<Impl>()) {}
WhaleD3D11RenderTarget::~WhaleD3D11RenderTarget() = default;

bool WhaleD3D11RenderTarget::Create(
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
        return Fail(error, L"Cannot create Whale Scene offscreen texture.");
    if (FAILED(device->CreateRenderTargetView(
            impl_->texture.Get(), nullptr, impl_->renderTargetView.GetAddressOf()))) {
        Reset();
        return Fail(error, L"Cannot create Whale Scene offscreen render-target view.");
    }
    if (FAILED(device->CreateShaderResourceView(
            impl_->texture.Get(), nullptr, impl_->shaderResourceView.GetAddressOf()))) {
        Reset();
        return Fail(error, L"Cannot create Whale Scene offscreen shader-resource view.");
    }

    impl_->width = width;
    impl_->height = height;
    if (error) error->clear();
    return true;
}

void WhaleD3D11RenderTarget::Reset() noexcept {
    impl_->shaderResourceView.Reset();
    impl_->renderTargetView.Reset();
    impl_->texture.Reset();
    impl_->width = 0;
    impl_->height = 0;
}

bool WhaleD3D11RenderTarget::Valid() const noexcept {
    return impl_->texture && impl_->renderTargetView && impl_->shaderResourceView;
}

unsigned WhaleD3D11RenderTarget::Width() const noexcept { return impl_->width; }
unsigned WhaleD3D11RenderTarget::Height() const noexcept { return impl_->height; }

ID3D11Texture2D* WhaleD3D11RenderTarget::Texture() const noexcept { return impl_ ? impl_->texture.Get() : nullptr; }

ID3D11RenderTargetView* WhaleD3D11RenderTarget::RenderTargetView() const noexcept {
    return impl_->renderTargetView.Get();
}

ID3D11ShaderResourceView* WhaleD3D11RenderTarget::ShaderResourceView() const noexcept {
    return impl_->shaderResourceView.Get();
}

struct WhaleD3D11RenderTargetPool::Impl {
    struct Entry {
        std::wstring id;
        std::unique_ptr<WhaleD3D11RenderTarget> target;
    };
    std::vector<Entry> entries;
};

WhaleD3D11RenderTargetPool::WhaleD3D11RenderTargetPool() : impl_(std::make_unique<Impl>()) {}
WhaleD3D11RenderTargetPool::~WhaleD3D11RenderTargetPool() = default;

bool WhaleD3D11RenderTargetPool::Build(
    ID3D11Device* device,
    const RenderGraphDefinition& graph,
    unsigned surfaceWidth,
    unsigned surfaceHeight,
    std::wstring* error) {
    Reset();
    if (!device) return Fail(error, L"D3D11 render-target pool device is null.");
    if (!WhaleRenderGraph::Validate(graph, error)) return false;

    for (const auto& resource : graph.resources) {
        if (resource.external) continue;

        unsigned width{};
        unsigned height{};
        if (!ResolveDimensions(resource, surfaceWidth, surfaceHeight, &width, &height, error)) {
            Reset();
            return false;
        }

        auto target = std::make_unique<WhaleD3D11RenderTarget>();
        if (!target->Create(device, width, height, error)) {
            Reset();
            return false;
        }
        impl_->entries.push_back({resource.id, std::move(target)});
    }

    if (error) error->clear();
    return true;
}

void WhaleD3D11RenderTargetPool::Reset() noexcept {
    impl_->entries.clear();
}

WhaleD3D11RenderTarget* WhaleD3D11RenderTargetPool::Find(std::wstring_view resourceId) noexcept {
    for (auto& entry : impl_->entries) if (entry.id == resourceId) return entry.target.get();
    return nullptr;
}

const WhaleD3D11RenderTarget* WhaleD3D11RenderTargetPool::Find(std::wstring_view resourceId) const noexcept {
    for (const auto& entry : impl_->entries) if (entry.id == resourceId) return entry.target.get();
    return nullptr;
}

std::size_t WhaleD3D11RenderTargetPool::Size() const noexcept {
    return impl_->entries.size();
}

} // namespace whaledesk::content
