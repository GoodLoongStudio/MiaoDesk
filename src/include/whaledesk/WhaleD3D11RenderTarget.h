#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "whaledesk/WhaleRenderGraph.h"

struct ID3D11Device;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace whaledesk::content {

class WhaleD3D11RenderTarget {
public:
    WhaleD3D11RenderTarget();
    ~WhaleD3D11RenderTarget();

    WhaleD3D11RenderTarget(const WhaleD3D11RenderTarget&) = delete;
    WhaleD3D11RenderTarget& operator=(const WhaleD3D11RenderTarget&) = delete;

    bool Create(ID3D11Device* device, unsigned width, unsigned height,
                std::wstring* error = nullptr);
    void Reset() noexcept;

    bool Valid() const noexcept;
    unsigned Width() const noexcept;
    unsigned Height() const noexcept;
    ID3D11Texture2D* Texture() const noexcept;
    ID3D11RenderTargetView* RenderTargetView() const noexcept;
    ID3D11ShaderResourceView* ShaderResourceView() const noexcept;

    static bool ValidateDimensions(unsigned width, unsigned height,
                                   std::wstring* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class WhaleD3D11RenderTargetPool {
public:
    WhaleD3D11RenderTargetPool();
    ~WhaleD3D11RenderTargetPool();

    WhaleD3D11RenderTargetPool(const WhaleD3D11RenderTargetPool&) = delete;
    WhaleD3D11RenderTargetPool& operator=(const WhaleD3D11RenderTargetPool&) = delete;

    bool Build(ID3D11Device* device, const RenderGraphDefinition& graph,
               unsigned surfaceWidth, unsigned surfaceHeight,
               std::wstring* error = nullptr);
    void Reset() noexcept;

    WhaleD3D11RenderTarget* Find(std::wstring_view resourceId) noexcept;
    const WhaleD3D11RenderTarget* Find(std::wstring_view resourceId) const noexcept;
    std::size_t Size() const noexcept;

    static bool ResolveDimensions(const RenderResourceDefinition& resource,
                                  unsigned surfaceWidth, unsigned surfaceHeight,
                                  unsigned* width, unsigned* height,
                                  std::wstring* error = nullptr);
    static bool SelfTest();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace whaledesk::content
