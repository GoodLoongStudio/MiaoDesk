#include "miaodesk/MiaoShaderContract.h"

#include <cctype>

namespace miaodesk::content {
namespace {

constexpr std::string_view kHlslPreamble = R"HLSL(
// Miao Shader Contract v1. Engine-owned ABI; user HLSL may build freely on top.
cbuffer MiaoFrame : register(b0)
{
    float MiaoTime;
    float MiaoDeltaTime;
    float2 MiaoResolution;

    float2 MiaoMousePosition;
    float2 MiaoMouseVelocity;

    float MiaoAudioVolume;
    float MiaoAudioBass;
    float MiaoAudioMid;
    float MiaoAudioTreble;
};

cbuffer MiaoObject : register(b1)
{
    float4x4 MiaoWorld;
    float4 MiaoObjectColor;
    float2 MiaoObjectSize;
    float MiaoObjectOpacity;
    float MiaoObjectReserved0;
};

// Parameter packing v1: parameters.json order is ABI-significant. Each parameter
// occupies one float4 slot. bool/int/float use .x; vec2/vec3/vec4/color use xy/xyz/xyzw.
// AI/authoring tools should append new parameters rather than reorder existing slots.
cbuffer MiaoParameters : register(b2)
{
    float4 MiaoParameter[16];
};

Texture2D MiaoInputTexture : register(t0);
Texture2D MiaoMaskTexture : register(t1);
// t8..t15 are reserved for package/user textures.
SamplerState MiaoLinearSampler : register(s0);
)HLSL";

bool IsIdentifierStart(unsigned char ch) noexcept {
    return std::isalpha(ch) != 0 || ch == '_';
}

bool IsIdentifierContinue(unsigned char ch) noexcept {
    return std::isalnum(ch) != 0 || ch == '_';
}

} // namespace

std::string_view MiaoShaderContract::HlslPreamble() noexcept {
    return kHlslPreamble;
}

bool MiaoShaderContract::IsStageSupportedByV1Contract(ShaderStage stage) noexcept {
    // Vertex + Pixel are first-class in v1. Compute is represented in the model
    // already, but requires a later dispatch/resource sandbox before execution.
    return stage == ShaderStage::Vertex || stage == ShaderStage::Pixel;
}

bool MiaoShaderContract::ValidateEntryPoint(std::string_view entryPoint, std::wstring* error) {
    if (entryPoint.empty()) {
        if (error) *error = L"Shader entry point cannot be empty.";
        return false;
    }
    if (!IsIdentifierStart(static_cast<unsigned char>(entryPoint.front()))) {
        if (error) *error = L"Shader entry point must be an HLSL identifier.";
        return false;
    }
    for (char ch : entryPoint.substr(1)) {
        if (!IsIdentifierContinue(static_cast<unsigned char>(ch))) {
            if (error) *error = L"Shader entry point must be an HLSL identifier.";
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

bool MiaoShaderContract::SelfTest() {
    std::wstring error;
    if (!ValidateEntryPoint("main", &error)) return false;
    if (!ValidateEntryPoint("MiaoPixel_01", &error)) return false;
    if (ValidateEntryPoint("bad-entry", &error)) return false;
    if (!IsStageSupportedByV1Contract(ShaderStage::Vertex)) return false;
    if (!IsStageSupportedByV1Contract(ShaderStage::Pixel)) return false;
    if (IsStageSupportedByV1Contract(ShaderStage::Compute)) return false;
    const auto source = HlslPreamble();
    return source.find("cbuffer MiaoFrame : register(b0)") != std::string_view::npos &&
           source.find("cbuffer MiaoParameters : register(b2)") != std::string_view::npos &&
           source.find("float4 MiaoParameter[16]") != std::string_view::npos;
}

} // namespace miaodesk::content
