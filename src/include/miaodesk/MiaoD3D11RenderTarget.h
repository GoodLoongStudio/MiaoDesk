#pragma once

#include <memory>
#include <string>

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

} // namespace miaodesk::content
