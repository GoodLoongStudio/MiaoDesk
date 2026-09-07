#include "miaodesk/MiaoD3D11RenderTarget.h"

#include <d3d11.h>
#include <dxgiformat.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

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

struct MiaoD3D11RenderTargetPool::Impl {
    struct Entry {
        std::wstring id;
        std::unique_ptr<MiaoD3D11RenderTarget> target;
    };
    std::vector<Entry> entries;
};

MiaoD3D11RenderTargetPool::MiaoD3D11RenderTargetPool() : impl_(std::make_unique<Impl>()) {}
MiaoD3D11RenderTargetPool::~MiaoD3D11RenderTargetPool() = default;

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

bool MiaoD3D11RenderTargetPool::Build(
    ID3D11Device* device,
    const RenderGraphDefinition& graph,
    unsigned surfaceWidth,
    unsigned surfaceHeight,
    std::wstring* error) {
    Reset();
    if (!device) return Fail(error, L"D3D11 render-target pool device is null.");
    if (!MiaoRenderGraph::Validate(graph, error)) return false;

    for (const auto& resource : graph.resources) {
        if (resource.external) continue;

        unsigned width{};
        unsigned height{};
        if (!ResolveDimensions(resource, surfaceWidth, surfaceHeight, &width, &height, error)) {
            Reset();
            return false;
        }

        auto target = std::make_unique<MiaoD3D11RenderTarget>();
        if (!target->Create(device, width, height, error)) {
            Reset();
            return false;
        }
        impl_->entries.push_back({resource.id, std::move(target)});
    }

    if (error) error->clear();
    return true;
}

void MiaoD3D11RenderTargetPool::Reset() noexcept {
    impl_->entries.clear();
}

MiaoD3D11RenderTarget* MiaoD3D11RenderTargetPool::Find(std::wstring_view resourceId) noexcept {
    for (auto& entry : impl_->entries) if (entry.id == resourceId) return entry.target.get();
    return nullptr;
}

const MiaoD3D11RenderTarget* MiaoD3D11RenderTargetPool::Find(std::wstring_view resourceId) const noexcept {
    for (const auto& entry : impl_->entries) if (entry.id == resourceId) return entry.target.get();
    return nullptr;
}

std::size_t MiaoD3D11RenderTargetPool::Size() const noexcept {
    return impl_->entries.size();
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

    return true;
}

} // namespace miaodesk::content
