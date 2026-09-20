#include "miaodesk/MiaoD3D11ParticleRenderer.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

std::wstring Utf8Diagnostic(const void* data, std::size_t size) {
    if (!data || size == 0) return {};
    const auto* bytes = static_cast<const char*>(data);
    const int needed = MultiByteToWideChar(CP_UTF8, 0, bytes, static_cast<int>(size), nullptr, 0);
    if (needed <= 0) return L"Shader compiler returned diagnostics.";
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes, static_cast<int>(size), out.data(), needed);
    return out;
}

bool CompileShader(
    std::string_view source,
    const char* entry,
    const char* target,
    ID3DBlob** bytecode,
    std::wstring* error) {
    if (!bytecode) return Fail(error, L"Particle shader output is null.");
    *bytecode = nullptr;
    ComPtr<ID3DBlob> diagnostics;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(
        source.data(), source.size(), "MiaoParticle", nullptr, nullptr,
        entry, target, flags, 0, bytecode, diagnostics.GetAddressOf());
    if (FAILED(hr)) {
        std::wstring message = L"Particle HLSL compile failed";
        if (diagnostics && diagnostics->GetBufferPointer() && diagnostics->GetBufferSize()) {
            message += L": ";
            message += Utf8Diagnostic(diagnostics->GetBufferPointer(), diagnostics->GetBufferSize());
        }
        return Fail(error, std::move(message));
    }
    return true;
}

constexpr std::string_view kParticleShader = R"HLSL(
cbuffer MiaoParticleFrame : register(b0)
{
    float2 MiaoParticleResolution;
    float2 MiaoParticleReserved;
};

struct MiaoParticleInstance
{
    float2 position;
    float size;
    float reserved;
    float4 color;
};

StructuredBuffer<MiaoParticleInstance> MiaoParticles : register(t2);

struct MiaoParticleVsOut
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float4 color : COLOR0;
};

MiaoParticleVsOut MiaoParticleVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {
        float2(-0.5, -0.5), float2(0.5, -0.5), float2(-0.5, 0.5),
        float2(-0.5, 0.5), float2(0.5, -0.5), float2(0.5, 0.5)
    };

    const MiaoParticleInstance item = MiaoParticles[instanceId];
    const float2 corner = corners[vertexId];
    const float2 pixel = item.position + corner * item.size;
    const float2 safeResolution = max(MiaoParticleResolution, float2(1.0, 1.0));

    MiaoParticleVsOut output;
    output.position = float4(
        pixel.x / safeResolution.x * 2.0 - 1.0,
        1.0 - pixel.y / safeResolution.y * 2.0,
        0.0,
        1.0);
    output.local = corner;
    output.color = item.color;
    return output;
}

float4 MiaoParticlePS(MiaoParticleVsOut input) : SV_Target
{
    const float distanceFromCenter = length(input.local);
    const float edge = 1.0 - smoothstep(0.38, 0.5, distanceFromCenter);
    return float4(input.color.rgb, input.color.a * edge);
}
)HLSL";

constexpr std::string_view kCopyShader = R"HLSL(
Texture2D MiaoParticleSceneColor : register(t0);
SamplerState MiaoParticleLinearSampler : register(s0);

struct MiaoCopyVsOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

MiaoCopyVsOut MiaoParticleCopyVS(uint vertexId : SV_VertexID)
{
    float2 position;
    if (vertexId == 0) position = float2(-1.0, -1.0);
    else if (vertexId == 1) position = float2(-1.0, 3.0);
    else position = float2(3.0, -1.0);

    MiaoCopyVsOut output;
    output.position = float4(position, 0.0, 1.0);
    output.uv = float2((position.x + 1.0) * 0.5, 1.0 - (position.y + 1.0) * 0.5);
    return output;
}

float4 MiaoParticleCopyPS(MiaoCopyVsOut input) : SV_Target
{
    return MiaoParticleSceneColor.Sample(MiaoParticleLinearSampler, input.uv);
}
)HLSL";

struct alignas(16) ParticleFrameConstants {
    float resolution[2]{};
    float reserved[2]{};
};
static_assert(sizeof(ParticleFrameConstants) == 16);

struct alignas(16) ParticleGpuInstance {
    float position[2]{};
    float size{};
    float reserved{};
    float color[4]{};
};
static_assert(sizeof(ParticleGpuInstance) == 32);

} // namespace

struct MiaoD3D11ParticleRenderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Buffer> particleBuffer;
    ComPtr<ID3D11ShaderResourceView> particleBufferView;
    ComPtr<ID3D11Buffer> frameBuffer;
    ComPtr<ID3D11VertexShader> particleVertexShader;
    ComPtr<ID3D11PixelShader> particlePixelShader;
    ComPtr<ID3D11VertexShader> copyVertexShader;
    ComPtr<ID3D11PixelShader> copyPixelShader;
    ComPtr<ID3D11SamplerState> linearSampler;
    ComPtr<ID3D11BlendState> alphaBlend;
    std::uint32_t capacity{};
    bool initialized{};

    bool Error(std::wstring* error, std::wstring message) {
        return Fail(error, std::move(message));
    }

    bool CreateBuffers(std::uint32_t nextCapacity, std::wstring* error) {
        D3D11_BUFFER_DESC particleDesc{};
        particleDesc.ByteWidth = static_cast<UINT>(sizeof(ParticleGpuInstance) * nextCapacity);
        particleDesc.Usage = D3D11_USAGE_DYNAMIC;
        particleDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        particleDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        particleDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        particleDesc.StructureByteStride = sizeof(ParticleGpuInstance);
        if (FAILED(device->CreateBuffer(&particleDesc, nullptr, particleBuffer.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle structured buffer.");

        D3D11_SHADER_RESOURCE_VIEW_DESC particleViewDesc{};
        particleViewDesc.Format = DXGI_FORMAT_UNKNOWN;
        particleViewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        particleViewDesc.Buffer.FirstElement = 0;
        particleViewDesc.Buffer.NumElements = nextCapacity;
        if (FAILED(device->CreateShaderResourceView(
                particleBuffer.Get(), &particleViewDesc, particleBufferView.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle structured-buffer SRV.");

        D3D11_BUFFER_DESC frameDesc{};
        frameDesc.ByteWidth = sizeof(ParticleFrameConstants);
        frameDesc.Usage = D3D11_USAGE_DYNAMIC;
        frameDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        frameDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&frameDesc, nullptr, frameBuffer.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle frame constant buffer.");
        return true;
    }

    bool CreateShaders(std::wstring* error) {
        ComPtr<ID3DBlob> particleVsCode;
        ComPtr<ID3DBlob> particlePsCode;
        ComPtr<ID3DBlob> copyVsCode;
        ComPtr<ID3DBlob> copyPsCode;

        if (!CompileShader(kParticleShader, "MiaoParticleVS", "vs_5_0", particleVsCode.GetAddressOf(), error) ||
            !CompileShader(kParticleShader, "MiaoParticlePS", "ps_5_0", particlePsCode.GetAddressOf(), error) ||
            !CompileShader(kCopyShader, "MiaoParticleCopyVS", "vs_5_0", copyVsCode.GetAddressOf(), error) ||
            !CompileShader(kCopyShader, "MiaoParticleCopyPS", "ps_5_0", copyPsCode.GetAddressOf(), error))
            return false;

        if (FAILED(device->CreateVertexShader(
                particleVsCode->GetBufferPointer(), particleVsCode->GetBufferSize(), nullptr,
                particleVertexShader.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle vertex shader.");
        if (FAILED(device->CreatePixelShader(
                particlePsCode->GetBufferPointer(), particlePsCode->GetBufferSize(), nullptr,
                particlePixelShader.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle pixel shader.");
        if (FAILED(device->CreateVertexShader(
                copyVsCode->GetBufferPointer(), copyVsCode->GetBufferSize(), nullptr,
                copyVertexShader.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle copy vertex shader.");
        if (FAILED(device->CreatePixelShader(
                copyPsCode->GetBufferPointer(), copyPsCode->GetBufferSize(), nullptr,
                copyPixelShader.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle copy pixel shader.");
        return true;
    }

    bool CreateStates(std::wstring* error) {
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler, linearSampler.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle sampler.");

        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blend, alphaBlend.GetAddressOf())))
            return Error(error, L"Cannot create Miao particle blend state.");
        return true;
    }

    bool UploadFrame(ID3D11DeviceContext* context, std::uint32_t width, std::uint32_t height, std::wstring* error) {
        ParticleFrameConstants constants{};
        constants.resolution[0] = static_cast<float>(std::max(1u, width));
        constants.resolution[1] = static_cast<float>(std::max(1u, height));
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(frameBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return Error(error, L"Cannot upload Miao particle frame constants.");
        *static_cast<ParticleFrameConstants*>(mapped.pData) = constants;
        context->Unmap(frameBuffer.Get(), 0);
        return true;
    }

    bool UploadParticles(ID3D11DeviceContext* context, const MiaoParticleRuntime& runtime, std::wstring* error) {
        const auto& particles = runtime.Particles();
        if (particles.size() > capacity)
            return Error(error, L"Live particle count exceeds the GPU particle-buffer capacity.");
        if (particles.empty()) return true;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(particleBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return Error(error, L"Cannot map Miao particle structured buffer.");
        auto* output = static_cast<ParticleGpuInstance*>(mapped.pData);
        for (std::size_t index = 0; index < particles.size(); ++index) {
            const auto& particle = particles[index];
            auto& gpu = output[index];
            gpu.position[0] = static_cast<float>(particle.position.x);
            gpu.position[1] = static_cast<float>(particle.position.y);
            gpu.size = static_cast<float>(std::max(0.0, particle.size));
            gpu.color[0] = static_cast<float>(particle.color.r);
            gpu.color[1] = static_cast<float>(particle.color.g);
            gpu.color[2] = static_cast<float>(particle.color.b);
            gpu.color[3] = static_cast<float>(particle.color.a);
        }
        context->Unmap(particleBuffer.Get(), 0);
        return true;
    }

    void SetViewport(ID3D11DeviceContext* context, std::uint32_t width, std::uint32_t height) noexcept {
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(std::max(1u, width));
        viewport.Height = static_cast<float>(std::max(1u, height));
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
    }

    bool Draw(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* output,
        ID3D11ShaderResourceView* sceneColor,
        std::uint32_t width,
        std::uint32_t height,
        const MiaoParticleRuntime& particles,
        std::wstring* error) {
        if (!initialized || !context || !output || !sceneColor || !particles.Initialized())
            return Error(error, L"Miao particle renderer draw state is incomplete.");
        if (!UploadFrame(context, width, height, error) || !UploadParticles(context, particles, error)) return false;

        ID3D11ShaderResourceView* nullSrvs[3]{};
        context->VSSetShaderResources(0, 3, nullSrvs);
        context->PSSetShaderResources(0, 3, nullSrvs);
        context->OMSetRenderTargets(1, &output, nullptr);
        SetViewport(context, width, height);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // First copy the previous scene color into this pass's single-writer
        // output target. Particle instances are then alpha-composited on top.
        context->VSSetShader(copyVertexShader.Get(), nullptr, 0);
        context->PSSetShader(copyPixelShader.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &sceneColor);
        ID3D11SamplerState* sampler = linearSampler.Get();
        context->PSSetSamplers(0, 1, &sampler);
        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        context->Draw(3, 0);
        ID3D11ShaderResourceView* nullScene{};
        context->PSSetShaderResources(0, 1, &nullScene);

        const auto liveCount = static_cast<UINT>(particles.LiveCount());
        if (liveCount > 0) {
            context->VSSetShader(particleVertexShader.Get(), nullptr, 0);
            context->PSSetShader(particlePixelShader.Get(), nullptr, 0);
            ID3D11Buffer* frame = frameBuffer.Get();
            context->VSSetConstantBuffers(0, 1, &frame);
            ID3D11ShaderResourceView* particleView = particleBufferView.Get();
            context->VSSetShaderResources(2, 1, &particleView);
            const float blendFactor[4]{};
            context->OMSetBlendState(alphaBlend.Get(), blendFactor, 0xFFFFFFFFu);
            context->DrawInstanced(6, liveCount, 0, 0);
            ID3D11ShaderResourceView* nullParticle{};
            context->VSSetShaderResources(2, 1, &nullParticle);
        }

        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        if (error) error->clear();
        return true;
    }

    void Reset() noexcept {
        alphaBlend.Reset();
        linearSampler.Reset();
        copyPixelShader.Reset();
        copyVertexShader.Reset();
        particlePixelShader.Reset();
        particleVertexShader.Reset();
        frameBuffer.Reset();
        particleBufferView.Reset();
        particleBuffer.Reset();
        device.Reset();
        capacity = 0;
        initialized = false;
    }
};

MiaoD3D11ParticleRenderer::MiaoD3D11ParticleRenderer() : impl_(std::make_unique<Impl>()) {}
MiaoD3D11ParticleRenderer::~MiaoD3D11ParticleRenderer() = default;

bool MiaoD3D11ParticleRenderer::Initialize(
    ID3D11Device* device,
    std::uint32_t maxParticles,
    std::wstring* error) {
    impl_->Reset();
    if (!device) return Fail(error, L"Miao particle renderer device is null.");
    if (maxParticles == 0 || maxParticles > MiaoSceneRuntimeModel::kMaxParticlesPerScene)
        return Fail(error, L"Miao particle renderer capacity exceeds the scene budget.");

    impl_->device = device;
    impl_->capacity = maxParticles;
    if (!impl_->CreateBuffers(maxParticles, error) ||
        !impl_->CreateShaders(error) ||
        !impl_->CreateStates(error)) {
        impl_->Reset();
        return false;
    }
    impl_->initialized = true;
    if (error) error->clear();
    return true;
}

bool MiaoD3D11ParticleRenderer::Draw(
    ID3D11DeviceContext* context,
    ID3D11RenderTargetView* output,
    ID3D11ShaderResourceView* sceneColor,
    std::uint32_t width,
    std::uint32_t height,
    const MiaoParticleRuntime& particles,
    std::wstring* error) {
    return impl_->Draw(context, output, sceneColor, width, height, particles, error);
}

void MiaoD3D11ParticleRenderer::Reset() noexcept { impl_->Reset(); }
bool MiaoD3D11ParticleRenderer::Initialized() const noexcept { return impl_->initialized; }
std::uint32_t MiaoD3D11ParticleRenderer::Capacity() const noexcept { return impl_->capacity; }

bool MiaoD3D11ParticleRenderer::SelfTest() {
    return sizeof(ParticleGpuInstance) == 32 &&
           MiaoSceneRuntimeModel::kMaxParticlesPerScene >= MiaoSceneRuntimeModel::kMaxParticlesPerEmitter;
}

} // namespace miaodesk::content
