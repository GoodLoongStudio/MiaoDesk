#include "whaledesk/WhalePostProcessShaderLibrary.h"

#include "whaledesk/WhaleShaderContract.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace whaledesk::content {
namespace {

std::string Header() {
    std::string source(WhaleShaderContract::HlslPreamble());
    source += R"HLSL(
cbuffer WhalePostProcess : register(b3)
{
    float WhalePostAmount;
    float WhalePostRadius;
    float WhalePostSoftness;
    float WhalePostTime;
    float2 WhalePostTexelSize;
    float2 WhalePostReserved;
};

// Post-process passes use the standard t0 input and may use t1 as an
// engine-wired auxiliary branch. The scene/material contract still exposes t1
// as WhaleMaskTexture; this alias keeps package HLSL ABI stable while the
// built-in post-process runtime can express branch/combine effects.
#define WhalePostAuxTexture WhaleMaskTexture

struct WhaleVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
)HLSL";
    return source;
}

std::string CopyBody() {
    return R"HLSL(
float4 WhalePostProcessMain(WhaleVertexOutput input) : SV_Target
{
    return WhaleInputTexture.Sample(WhaleLinearSampler, input.uv);
}
)HLSL";
}

std::string VignetteBody() {
    return R"HLSL(
float4 WhalePostProcessMain(WhaleVertexOutput input) : SV_Target
{
    float4 color = WhaleInputTexture.Sample(WhaleLinearSampler, input.uv);
    float2 p = input.uv * 2.0 - 1.0;
    float distanceFromCenter = length(p);
    float edge = saturate((distanceFromCenter - WhalePostRadius) / max(WhalePostSoftness, 0.0001));
    color.rgb *= 1.0 - saturate(edge * WhalePostAmount);
    return color;
}
)HLSL";
}

std::string NoiseBody() {
    return R"HLSL(
float WhaleHash(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233)) + WhalePostTime * 19.19) * 43758.5453);
}

float4 WhalePostProcessMain(WhaleVertexOutput input) : SV_Target
{
    float4 color = WhaleInputTexture.Sample(WhaleLinearSampler, input.uv);
    float noise = WhaleHash(input.uv * max(WhaleResolution, float2(1.0, 1.0))) - 0.5;
    color.rgb += noise * WhalePostAmount * 0.12;
    return color;
}
)HLSL";
}

std::string ColorMatrixBody() {
    return R"HLSL(
float4 WhalePostProcessMain(WhaleVertexOutput input) : SV_Target
{
    float4 color = WhaleInputTexture.Sample(WhaleLinearSampler, input.uv);
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float saturation = max(0.0, WhalePostAmount);
    color.rgb = lerp(float3(luminance, luminance, luminance), color.rgb, saturation);
    return color;
}
)HLSL";
}

std::string BlurBody(bool horizontal) {
    const char* axis = horizontal ? "float2(WhalePostTexelSize.x, 0.0)" : "float2(0.0, WhalePostTexelSize.y)";
    std::string body = R"HLSL(
float4 WhalePostProcessMain(WhaleVertexOutput input) : SV_Target
{
    float2 stepUv = )HLSL";
    body += axis;
    body += R"HLSL( * max(1.0, WhalePostRadius * 6.0);
    float4 color = WhaleInputTexture.Sample(WhaleLinearSampler, input.uv) * 0.227027;
    color += WhaleInputTexture.Sample(WhaleLinearSampler, input.uv + stepUv * 1.384615) * 0.316216;
    color += WhaleInputTexture.Sample(WhaleLinearSampler, input.uv - stepUv * 1.384615) * 0.316216;
    color += WhaleInputTexture.Sample(WhaleLinearSampler, input.uv + stepUv * 3.230769) * 0.070270;
    color += WhaleInputTexture.Sample(WhaleLinearSampler, input.uv - stepUv * 3.230769) * 0.070270;
    return color;
}
)HLSL";
    return body;
}

std::string BloomThresholdBody() {
    return R"HLSL(
float4 WhalePostProcessMain(WhaleVertexOutput input) : SV_Target
{
    float4 color = WhaleInputTexture.Sample(WhaleLinearSampler, input.uv);
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float threshold = saturate(WhalePostRadius);
    float knee = max(WhalePostSoftness, 0.0001);
    float contribution = smoothstep(threshold - knee, threshold + knee, luminance);
    return float4(color.rgb * contribution * WhalePostAmount, color.a * contribution);
}
)HLSL";
}

std::string BloomCombineBody() {
    return R"HLSL(
float4 WhalePostProcessMain(WhaleVertexOutput input) : SV_Target
{
    float4 bloom = WhaleInputTexture.Sample(WhaleLinearSampler, input.uv);
    float4 scene = WhalePostAuxTexture.Sample(WhaleLinearSampler, input.uv);
    scene.rgb += bloom.rgb * max(0.0, WhalePostAmount);
    return scene;
}
)HLSL";
}

} // namespace

std::string WhalePostProcessShaderLibrary::PixelShaderSource(PostProcessEffectKind effect) {
    auto source = Header();
    switch (effect) {
    case PostProcessEffectKind::Copy:
        source += CopyBody();
        break;
    case PostProcessEffectKind::Vignette:
        source += VignetteBody();
        break;
    case PostProcessEffectKind::Noise:
        source += NoiseBody();
        break;
    case PostProcessEffectKind::ColorMatrix:
        source += ColorMatrixBody();
        break;
    case PostProcessEffectKind::BlurHorizontal:
        source += BlurBody(true);
        break;
    case PostProcessEffectKind::BlurVertical:
        source += BlurBody(false);
        break;
    case PostProcessEffectKind::BloomThreshold:
        source += BloomThresholdBody();
        break;
    case PostProcessEffectKind::BloomCombine:
        source += BloomCombineBody();
        break;
    }
    return source;
}

std::string_view WhalePostProcessShaderLibrary::EffectKey(PostProcessEffectKind effect) noexcept {
    switch (effect) {
    case PostProcessEffectKind::Copy: return "copy";
    case PostProcessEffectKind::Vignette: return "vignette";
    case PostProcessEffectKind::Noise: return "noise";
    case PostProcessEffectKind::ColorMatrix: return "colorMatrix";
    case PostProcessEffectKind::BlurHorizontal: return "blurHorizontal";
    case PostProcessEffectKind::BlurVertical: return "blurVertical";
    case PostProcessEffectKind::BloomThreshold: return "bloomThreshold";
    case PostProcessEffectKind::BloomCombine: return "bloomCombine";
    }
    return "copy";
}

bool WhalePostProcessShaderLibrary::ParseEffectKey(
    std::string_view key,
    PostProcessEffectKind* effect) noexcept {
    if (!effect) return false;
    if (key == "copy") *effect = PostProcessEffectKind::Copy;
    else if (key == "vignette") *effect = PostProcessEffectKind::Vignette;
    else if (key == "noise") *effect = PostProcessEffectKind::Noise;
    else if (key == "colorMatrix") *effect = PostProcessEffectKind::ColorMatrix;
    else if (key == "blurHorizontal") *effect = PostProcessEffectKind::BlurHorizontal;
    else if (key == "blurVertical") *effect = PostProcessEffectKind::BlurVertical;
    else if (key == "bloomThreshold") *effect = PostProcessEffectKind::BloomThreshold;
    else if (key == "bloomCombine") *effect = PostProcessEffectKind::BloomCombine;
    else return false;
    return true;
}

WhalePostProcessConstants WhalePostProcessShaderLibrary::MakeConstants(
    const PostProcessDefinition& definition,
    unsigned width,
    unsigned height,
    float timeSeconds) noexcept {
    WhalePostProcessConstants constants;
    constants.amount = static_cast<float>(definition.amount);
    constants.radius = static_cast<float>(definition.radius);
    constants.softness = static_cast<float>(definition.softness);
    constants.time = std::isfinite(timeSeconds) ? timeSeconds : 0.0f;
    constants.texelSize[0] = 1.0f / static_cast<float>(std::max(1u, width));
    constants.texelSize[1] = 1.0f / static_cast<float>(std::max(1u, height));
    return constants;
}

bool WhalePostProcessShaderLibrary::SelfTest() {
    constexpr PostProcessEffectKind effects[] = {
        PostProcessEffectKind::Copy,
        PostProcessEffectKind::Vignette,
        PostProcessEffectKind::Noise,
        PostProcessEffectKind::ColorMatrix,
        PostProcessEffectKind::BlurHorizontal,
        PostProcessEffectKind::BlurVertical,
        PostProcessEffectKind::BloomThreshold,
        PostProcessEffectKind::BloomCombine,
    };

    for (const auto effect : effects) {
        const auto key = EffectKey(effect);
        PostProcessEffectKind roundTrip{};
        if (key.empty() || !ParseEffectKey(key, &roundTrip) || roundTrip != effect) return false;
        const auto source = PixelShaderSource(effect);
        if (source.find("cbuffer WhalePostProcess : register(b3)") == std::string::npos) return false;
        if (source.find(std::string(kEntryPoint)) == std::string::npos) return false;
    }

    const auto combineSource = PixelShaderSource(PostProcessEffectKind::BloomCombine);
    if (combineSource.find("WhalePostAuxTexture") == std::string::npos) return false;

    PostProcessDefinition definition;
    definition.amount = 0.8;
    definition.radius = 0.6;
    definition.softness = 0.2;
    const auto constants = MakeConstants(definition, 1920, 1080, 1.25f);
    if (constants.amount <= 0.0f || constants.texelSize[0] <= 0.0f || constants.texelSize[1] <= 0.0f) return false;

    PostProcessEffectKind invalid{};
    if (ParseEffectKey("not-an-effect", &invalid)) return false;
    return true;
}

} // namespace whaledesk::content
