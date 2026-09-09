#pragma once

#include <string>
#include <string_view>

#include "miaodesk/MiaoSceneModel.h"

namespace miaodesk::content {

class MiaoShaderContract {
public:
    static constexpr unsigned kVersion = 1;

    // Reserved D3D constant-buffer / resource slots for user-authored shaders.
    static constexpr unsigned kFrameCBufferRegister = 0;
    static constexpr unsigned kObjectCBufferRegister = 1;
    static constexpr unsigned kParameterCBufferRegister = 2;
    static constexpr unsigned kMaterialCBufferRegister = kParameterCBufferRegister;
    static constexpr unsigned kInputTextureRegister = 0;
    static constexpr unsigned kMaskTextureRegister = 1;
    static constexpr unsigned kFirstUserTextureRegister = 8;
    static constexpr unsigned kLastUserTextureRegister = 15;
    static constexpr unsigned kLinearSamplerRegister = 0;

    static std::string_view HlslPreamble() noexcept;
    static bool IsStageSupportedByV1Contract(ShaderStage stage) noexcept;
    static bool ValidateEntryPoint(std::string_view entryPoint, std::wstring* error = nullptr);
    static bool SelfTest();
};

} // namespace miaodesk::content
