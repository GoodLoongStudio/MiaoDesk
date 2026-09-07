#pragma once

#include <filesystem>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace miaodesk::content {

class MiaoD3D11TextureLoader {
public:
    static bool LoadImage(
        ID3D11Device* device,
        const std::filesystem::path& path,
        ID3D11ShaderResourceView** view,
        std::wstring* error = nullptr);

    static bool SelfTestPathPolicy();
};

} // namespace miaodesk::content
