#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "miaodesk/MiaoRenderGraph.h"

struct ID3D11Device;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace miaodesk::content {

class MiaoD3D11RenderTarget {
public:
    MiaoD3D11RenderTarget();
    ~MiaoD3D11RenderTarget();

    MiaoD3D11RenderTarget(const MiaoD3D11RenderTarget&) = delete;
    MiaoD3D11RenderTarget& operator=(const MiaoD3D11RenderTarget&) = delete;

    bool Create(ID3D11Device* device, unsigned width, unsigned height,
                std::wstring* error = nullptr);
    void Reset() noexcept;

    bool Valid() const noexcept;
    unsigned Width() const noexcept;
    unsigned Height() const noexcept;
    ID3D11RenderTargetView* RenderTargetView() const noexcept;
    ID3D11ShaderResourceView* ShaderResourceView() const noexcept;

    static bool ValidateDimensions(unsigned width, unsigned height,
                                   std::wstring* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class MiaoD3D11RenderTargetPool {
public:
    MiaoD3D11RenderTargetPool();
    ~MiaoD3D11RenderTargetPool();

    MiaoD3D11RenderTargetPool(const MiaoD3D11RenderTargetPool&) = delete;
    MiaoD3D11RenderTargetPool& operator=(const MiaoD3D11RenderTargetPool&) = delete;

    bool Build(ID3D11Device* device, const RenderGraphDefinition& graph,
               unsigned surfaceWidth, unsigned surfaceHeight,
               std::wstring* error = nullptr);
    void Reset() noexcept;

    MiaoD3D11RenderTarget* Find(std::wstring_view resourceId) noexcept;
    const MiaoD3D11RenderTarget* Find(std::wstring_view resourceId) const noexcept;
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

} // namespace miaodesk::content
