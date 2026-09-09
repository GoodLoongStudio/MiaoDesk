#pragma once

#include <filesystem>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace miaodesk::content {

class MiaoD3D11TextureLoader {
public:
    // The W suffix is intentional: <windows.h> defines LoadImage as an A/W
    // macro. Keeping the internal Windows renderer entry point explicitly
    // named LoadImageW prevents the preprocessor from changing the qualified
    // member name differently between declaration and implementation units.
    static bool LoadImageW(
        ID3D11Device* device,
        const std::filesystem::path& path,
        ID3D11ShaderResourceView** view,
        std::wstring* error = nullptr);

    static bool SelfTestPathPolicy();
};

} // namespace miaodesk::content
