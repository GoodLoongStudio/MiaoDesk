#include "miaodesk/MiaoSceneD3D11Renderer.h"

#include "miaodesk/MiaoAssetDatabase.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoD3D11RenderTarget.h"
#include "miaodesk/MiaoD3D11TextureLoader.h"
#include "miaodesk/MiaoGpuParameterBlock.h"
#include "miaodesk/MiaoRenderGraph.h"
#include "miaodesk/MiaoSceneRuntime.h"
#include "miaodesk/MiaoSceneSerializer.h"
#include "miaodesk/MiaoShaderContract.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

std::string ReadTextFile(const fs::path& path, std::size_t maxBytes = 4 * 1024 * 1024) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || static_cast<unsigned long long>(length) > maxBytes) return {};
    input.seekg(0, std::ios::beg);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
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

const SceneComponentDefinition* FindRenderable(const SceneRuntimeDefinition& definition) {
    for (const auto& node : definition.scene.nodes) {
        if (!node.enabled) continue;
        for (const auto& component : node.components) {
            if (component.kind == ComponentKind::SpriteRenderer) return &component;
        }
    }
    return nullptr;
}

const PropertyDefinition* FindComponentProperty(
    const SceneComponentDefinition& component, std::wstring_view name) {
    for (const auto& property : component.properties) if (property.name == name) return &property;
    return nullptr;
}

const PropertyDefinition* FindMaterialProperty(
    const MaterialDefinition& material, std::wstring_view name) {
    for (const auto& property : material.properties) if (property.name == name) return &property;
    return nullptr;
}

const ShaderDefinition* FindShader(const SceneDefinition& scene, std::wstring_view id) {
    for (const auto& shader : scene.shaders) if (shader.id == id) return &shader;
    return nullptr;
}

std::wstring MaterialIdFor(
    const SceneComponentDefinition& component,
    const MiaoSceneRuntime& runtime) {
    if (const auto* current = runtime.GetProperty(PropertyAddress{component.id, L"materialId"})) {
        if (const auto* id = std::get_if<std::wstring>(current)) return *id;
    }
    if (const auto* property = FindComponentProperty(component, L"materialId")) {
        if (const auto* id = std::get_if<std::wstring>(&property->defaultValue)) return *id;
    }
    return {};
}

Color4 ReadColor(const PropertyValue* value, Color4 fallback) {
    const auto* color = value ? std::get_if<Color4>(value) : nullptr;
    return color ? *color : fallback;
}

double ReadFloat(const PropertyValue* value, double fallback) {
    const auto* number = value ? std::get_if<double>(value) : nullptr;
    return number && std::isfinite(*number) ? *number : fallback;
}

int TextureRegisterForSlot(std::wstring_view slot) {
    if (slot == L"input" || slot == L"inputTexture" || slot == L"t0") return 0;
    if (slot == L"mask" || slot == L"maskTexture" || slot == L"t1") return 1;
    if (slot.size() == 5 && slot.substr(0, 4) == L"user" && slot[4] >= L'0' && slot[4] <= L'7')
        return 8 + static_cast<int>(slot[4] - L'0');
    if (slot.size() >= 2 && slot[0] == L't') {
        unsigned value = 0;
        for (wchar_t ch : slot.substr(1)) {
            if (ch < L'0' || ch > L'9') return -1;
            value = value * 10u + static_cast<unsigned>(ch - L'0');
        }
        if (value == 0 || value == 1 || (value >= 8 && value <= 15)) return static_cast<int>(value);
    }
    return -1;
}

std::string EngineVertexShader() {
    std::string source(MiaoShaderContract::HlslPreamble());
    source += R"HLSL(
struct MiaoVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

MiaoVertexOutput MiaoBuiltinVertex(uint vertexId : SV_VertexID)
{
    MiaoVertexOutput output;
    float2 position;
    if (vertexId == 0) position = float2(-1.0, -1.0);
    else if (vertexId == 1) position = float2(-1.0, 3.0);
    else position = float2(3.0, -1.0);
    output.position = float4(position, 0.0, 1.0);
    output.uv = float2((position.x + 1.0) * 0.5, 1.0 - (position.y + 1.0) * 0.5);
    return output;
}
)HLSL";
    return source;
}

std::string EngineSolidPixelShader() {
    std::string source(MiaoShaderContract::HlslPreamble());
    source += R"HLSL(
struct MiaoVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 MiaoBuiltinSolid(MiaoVertexOutput input) : SV_Target
{
    return float4(MiaoObjectColor.rgb, MiaoObjectColor.a * MiaoObjectOpacity);
}
)HLSL";
    return source;
}

std::string EngineCompositePixelShader() {
    std::string source(MiaoShaderContract::HlslPreamble());
    source += R"HLSL(
struct MiaoVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 MiaoBuiltinComposite(MiaoVertexOutput input) : SV_Target
{
    return MiaoInputTexture.Sample(MiaoLinearSampler, input.uv);
}
)HLSL";
    return source;
}

bool CompileShader(
    std::string_view source,
    std::string_view entry,
    const char* target,
    ComPtr<ID3DBlob>* bytecode,
    std::wstring* error) {
    if (!bytecode) return Fail(error, L"Shader output is null.");
    bytecode->Reset();
    ComPtr<ID3DBlob> diagnostics;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT hr = D3DCompile(
        source.data(), source.size(), "MiaoScene", nullptr, nullptr,
        std::string(entry).c_str(), target, flags, 0,
        bytecode->GetAddressOf(), diagnostics.GetAddressOf());
    if (FAILED(hr)) {
        std::wstring message = L"HLSL compile failed";
        if (diagnostics && diagnostics->GetBufferPointer() && diagnostics->GetBufferSize()) {
            message += L": ";
            message += Utf8Diagnostic(diagnostics->GetBufferPointer(), diagnostics->GetBufferSize());
        }
        return Fail(error, std::move(message));
    }
    if (error) error->clear();
    return true;
}

struct alignas(16) FrameConstants {
    float time{};
    float deltaTime{};
    float resolution[2]{};
    float mousePosition[2]{};
    float mouseVelocity[2]{};
    float audioVolume{};
    float audioBass{};
    float audioMid{};
    float audioTreble{};
};
static_assert(sizeof(FrameConstants) == 48);

struct alignas(16) ObjectConstants {
    float world[16]{};
    float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    float size[2]{};
    float opacity{1.0f};
    float reserved{};
};
static_assert(sizeof(ObjectConstants) == 96);
static_assert(sizeof(MiaoGpuParameterBlock) == 16 * 16);

void SetIdentity(float (&matrix)[16]) {
    std::fill(std::begin(matrix), std::end(matrix), 0.0f);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

} // namespace

struct MiaoSceneD3D11Renderer::Impl {
    HWND window{};
    LoadedMiaoContentPackage package;
    SceneRuntimeDefinition definition;
    MiaoAssetDatabase assets;
    MiaoSceneRuntime runtime;
    const SceneComponentDefinition* renderable{};
    const MaterialDefinition* material{};

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> backbufferRenderTargetView;
    MiaoD3D11RenderTarget sceneColor;

    ComPtr<ID3D11VertexShader> fullscreenVertexShader;
    ComPtr<ID3D11VertexShader> sceneVertexShader;
    ComPtr<ID3D11PixelShader> scenePixelShader;
    ComPtr<ID3D11PixelShader> compositePixelShader;
    ComPtr<ID3D11Buffer> frameBuffer;
    ComPtr<ID3D11Buffer> objectBuffer;
    ComPtr<ID3D11Buffer> parameterBuffer;
    ComPtr<ID3D11SamplerState> linearSampler;
    ComPtr<ID3D11BlendState> alphaBlend;
    std::array<ComPtr<ID3D11ShaderResourceView>, 16> textureViews;

    RenderGraphDefinition renderGraph;
    CompiledRenderGraph compiledGraph;
    std::wstring lastError;
    unsigned width{};
    unsigned height{};
    float previousTime{};
    POINT previousMouse{};
    bool hasPreviousMouse{};
    bool programmable{};
    bool loaded{};

    bool Error(std::wstring* error, std::wstring message) {
        lastError = std::move(message);
        if (error) *error = lastError;
        return false;
    }

    void UnbindShaderResources() noexcept {
        if (!context) return;
        ID3D11ShaderResourceView* nullViews[16]{};
        context->PSSetShaderResources(0, 16, nullViews);
    }

    void SetViewport(unsigned targetWidth, unsigned targetHeight) noexcept {
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(targetWidth);
        viewport.Height = static_cast<float>(targetHeight);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
    }

    bool CreateDeviceAndSwapChain(std::wstring* error) {
        RECT rect{};
        if (!GetClientRect(window, &rect)) return Error(error, L"Cannot query D3D11 scene surface size.");
        width = static_cast<unsigned>(std::max<LONG>(1, rect.right - rect.left));
        height = static_cast<unsigned>(std::max<LONG>(1, rect.bottom - rect.top));

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = width;
        desc.BufferDesc.Height = height;
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        D3D_FEATURE_LEVEL selected{};
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        auto create = [&](D3D_DRIVER_TYPE driver) {
            device.Reset();
            context.Reset();
            swapChain.Reset();
            return D3D11CreateDeviceAndSwapChain(
                nullptr, driver, nullptr, flags,
                levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                &desc, swapChain.GetAddressOf(), device.GetAddressOf(), &selected,
                context.GetAddressOf());
        };

        HRESULT hr = create(D3D_DRIVER_TYPE_HARDWARE);
        if (FAILED(hr)) hr = create(D3D_DRIVER_TYPE_WARP);
        if (FAILED(hr)) return Error(error, L"Cannot create D3D11 device/swap-chain for Miao Scene.");
        if (!CreateBackbuffer(error)) return false;
        return CreateSceneColor(error);
    }

    bool CreateBackbuffer(std::wstring* error) {
        backbufferRenderTargetView.Reset();
        ComPtr<ID3D11Texture2D> backbuffer;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(backbuffer.GetAddressOf()))))
            return Error(error, L"Cannot obtain Miao Scene D3D11 backbuffer.");
        if (FAILED(device->CreateRenderTargetView(
                backbuffer.Get(), nullptr, backbufferRenderTargetView.GetAddressOf())))
            return Error(error, L"Cannot create Miao Scene D3D11 backbuffer render target.");
        return true;
    }

    bool CreateSceneColor(std::wstring* error) {
        std::wstring targetError;
        if (!sceneColor.Create(device.Get(), width, height, &targetError))
            return Error(error, targetError.empty() ? L"Cannot create SceneColor render target." : targetError);
        return true;
    }

    bool CreateConstantBuffer(UINT size, ID3D11Buffer** output, std::wstring* error) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = (size + 15u) & ~15u;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&desc, nullptr, output)))
            return Error(error, L"Cannot create Miao Scene constant buffer.");
        return true;
    }

    bool CreateStates(std::wstring* error) {
        if (!CreateConstantBuffer(sizeof(FrameConstants), frameBuffer.GetAddressOf(), error)) return false;
        if (!CreateConstantBuffer(sizeof(ObjectConstants), objectBuffer.GetAddressOf(), error)) return false;
        if (!CreateConstantBuffer(sizeof(MiaoGpuParameterBlock), parameterBuffer.GetAddressOf(), error)) return false;

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler, linearSampler.GetAddressOf())))
            return Error(error, L"Cannot create Miao Scene sampler.");

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
            return Error(error, L"Cannot create Miao Scene blend state.");
        return true;
    }

    bool ResolveSceneMaterial(std::wstring* error) {
        renderable = FindRenderable(definition);
        if (!renderable) return Error(error, L"Scene has no SpriteRenderer for the D3D11 MVP backend.");
        const auto materialId = MaterialIdFor(*renderable, runtime);
        material = materialId.empty() ? nullptr : MiaoSceneRuntimeModel::FindMaterial(definition, materialId);
        if (!material) return Error(error, L"Scene SpriteRenderer does not resolve a material.");
        programmable = material->model == MaterialModel::Programmable;
        if (!programmable && material->builtinName != L"solidColor")
            return Error(error, L"D3D11 MVP currently supports builtin solidColor or programmable materials.");
        return true;
    }

    bool CreateTextures(std::wstring* error) {
        for (auto& view : textureViews) view.Reset();
        for (const auto& binding : material->textures) {
            const int registerIndex = TextureRegisterForSlot(binding.slot);
            if (registerIndex < 0 || registerIndex >= static_cast<int>(textureViews.size()))
                return Error(error, L"Unknown Miao Shader texture slot: " + binding.slot);
            if (textureViews[static_cast<std::size_t>(registerIndex)])
                return Error(error, L"Duplicate Miao Shader texture register: " + binding.slot);
            const auto* asset = assets.Find(binding.asset.id);
            if (!asset) return Error(error, L"Material texture asset is missing: " + binding.asset.id);
            if (asset->type != AssetType::Image)
                return Error(error, L"D3D11 v1 texture binding currently accepts image assets only: " + binding.asset.id);
            std::wstring loadError;
            if (!MiaoD3D11TextureLoader::LoadImage(
                    device.Get(), asset->resolvedPath,
                    textureViews[static_cast<std::size_t>(registerIndex)].GetAddressOf(), &loadError))
                return Error(error, loadError);
        }
        return true;
    }

    bool LoadShaderSource(std::wstring_view shaderId, ShaderStage stage, std::string* source,
                          std::string* entry, std::wstring* error) {
        if (!source || !entry) return Error(error, L"Shader source output is null.");
        const auto* shader = FindShader(definition.scene, shaderId);
        if (!shader || shader->stage != stage)
            return Error(error, L"Programmable material references an invalid shader stage.");
        if (!MiaoShaderContract::IsStageSupportedByV1Contract(stage))
            return Error(error, L"Shader stage is not executable by Miao Shader Contract v1.");
        std::wstring entryError;
        if (!MiaoShaderContract::ValidateEntryPoint(shader->entryPoint, &entryError)) return Error(error, entryError);
        const auto* asset = assets.Find(shader->assetId);
        if (!asset || asset->type != AssetType::Shader)
            return Error(error, L"Shader source asset is missing from the package asset database.");
        const auto userSource = ReadTextFile(asset->resolvedPath);
        if (userSource.empty()) return Error(error, L"Shader source file is empty or unreadable.");
        source->assign(MiaoShaderContract::HlslPreamble());
        source->append("\n");
        source->append(userSource);
        *entry = shader->entryPoint;
        return true;
    }

    bool CreateShaders(std::wstring* error) {
        ComPtr<ID3DBlob> fullscreenVsCode;
        ComPtr<ID3DBlob> sceneVsCode;
        ComPtr<ID3DBlob> scenePsCode;
        ComPtr<ID3DBlob> compositePsCode;

        const auto fullscreenSource = EngineVertexShader();
        if (!CompileShader(fullscreenSource, "MiaoBuiltinVertex", "vs_5_0", &fullscreenVsCode, error)) return false;
        if (FAILED(device->CreateVertexShader(
                fullscreenVsCode->GetBufferPointer(), fullscreenVsCode->GetBufferSize(), nullptr,
                fullscreenVertexShader.GetAddressOf())))
            return Error(error, L"Cannot create Miao Scene fullscreen vertex shader.");

        if (programmable && !material->vertexShaderId.empty()) {
            std::string source;
            std::string entry;
            if (!LoadShaderSource(material->vertexShaderId, ShaderStage::Vertex, &source, &entry, error)) return false;
            if (!CompileShader(source, entry, "vs_5_0", &sceneVsCode, error)) return false;
            if (FAILED(device->CreateVertexShader(
                    sceneVsCode->GetBufferPointer(), sceneVsCode->GetBufferSize(), nullptr,
                    sceneVertexShader.GetAddressOf())))
                return Error(error, L"Cannot create programmable Miao Scene vertex shader.");
        } else {
            sceneVertexShader = fullscreenVertexShader;
        }

        if (programmable) {
            std::string source;
            std::string entry;
            if (!LoadShaderSource(material->pixelShaderId, ShaderStage::Pixel, &source, &entry, error)) return false;
            if (!CompileShader(source, entry, "ps_5_0", &scenePsCode, error)) return false;
        } else {
            const auto source = EngineSolidPixelShader();
            if (!CompileShader(source, "MiaoBuiltinSolid", "ps_5_0", &scenePsCode, error)) return false;
        }
        if (FAILED(device->CreatePixelShader(
                scenePsCode->GetBufferPointer(), scenePsCode->GetBufferSize(), nullptr,
                scenePixelShader.GetAddressOf())))
            return Error(error, L"Cannot create Miao Scene pixel shader.");

        const auto compositeSource = EngineCompositePixelShader();
        if (!CompileShader(compositeSource, "MiaoBuiltinComposite", "ps_5_0", &compositePsCode, error)) return false;
        if (FAILED(device->CreatePixelShader(
                compositePsCode->GetBufferPointer(), compositePsCode->GetBufferSize(), nullptr,
                compositePixelShader.GetAddressOf())))
            return Error(error, L"Cannot create Miao Scene composite pixel shader.");
        return true;
    }

    bool BuildRenderGraph(std::wstring* error) {
        renderGraph = {};

        RenderResourceDefinition sceneResource;
        sceneResource.id = L"renderres://scene-color";
        sceneResource.external = false;
        sceneResource.persistent = false;
        sceneResource.format = RenderResourceFormat::Bgra8Unorm;
        sceneResource.sizePolicy = RenderResourceSizePolicy::SurfaceRelative;
        sceneResource.widthScale = 1.0f;
        sceneResource.heightScale = 1.0f;
        sceneResource.renderTarget = true;
        sceneResource.shaderResource = true;

        RenderResourceDefinition backbufferResource;
        backbufferResource.id = L"renderres://backbuffer";
        backbufferResource.external = true;
        backbufferResource.persistent = false;
        backbufferResource.renderTarget = true;
        backbufferResource.shaderResource = false;

        renderGraph.resources = {sceneResource, backbufferResource};
        renderGraph.passes = {
            {
                programmable ? L"renderpass://programmable-scene" : L"renderpass://scene",
                programmable ? RenderPassKind::Programmable : RenderPassKind::Scene2D,
                {},
                {L"renderres://scene-color"},
                true,
            },
            {
                L"renderpass://composite",
                RenderPassKind::Composite,
                {L"renderres://scene-color"},
                {L"renderres://backbuffer"},
                true,
            },
            {
                L"renderpass://present",
                RenderPassKind::Present,
                {L"renderres://backbuffer"},
                {},
                true,
            },
        };
        return MiaoRenderGraph::Compile(renderGraph, &compiledGraph, error);
    }

    bool Load(const fs::path& packageRoot, HWND nextWindow, std::wstring* error) {
        Reset();
        if (!nextWindow || !IsWindow(nextWindow)) return Error(error, L"Miao Scene D3D11 window is invalid.");
        window = nextWindow;

        if (!MiaoContentPackage::Load(packageRoot, &package, &lastError)) return Error(error, lastError);
        if (package.manifest.kind != ContentKind::Wallpaper || package.manifest.runtime != ContentRuntimeKind::Scene)
            return Error(error, L"Miao Scene D3D11 renderer requires a wallpaper scene package.");
        if (!MiaoSceneSerializer::DeserializePackage(package, &definition, &lastError)) return Error(error, lastError);
        if (!assets.Build(package.root, definition, &lastError)) return Error(error, lastError);
        if (!runtime.Initialize(definition, &lastError)) return Error(error, lastError);
        if (!ResolveSceneMaterial(error)) return false;
        if (!CreateDeviceAndSwapChain(error)) return false;
        if (!CreateStates(error)) return false;
        if (!CreateTextures(error)) return false;
        if (!CreateShaders(error)) return false;
        if (!BuildRenderGraph(error)) return false;

        loaded = true;
        lastError.clear();
        if (error) error->clear();
        return true;
    }

    template <typename T>
    bool Upload(ID3D11Buffer* buffer, const T& value, std::wstring* error) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return Error(error, L"Cannot update Miao Scene constant buffer.");
        *static_cast<T*>(mapped.pData) = value;
        context->Unmap(buffer, 0);
        return true;
    }

    bool BindSceneState(
        const FrameConstants& frame,
        const ObjectConstants& object,
        const MiaoGpuParameterBlock& parameters,
        std::wstring* error) {
        if (!Upload(frameBuffer.Get(), frame, error)) return false;
        if (!Upload(objectBuffer.Get(), object, error)) return false;
        if (!Upload(parameterBuffer.Get(), parameters, error)) return false;

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(sceneVertexShader.Get(), nullptr, 0);
        context->PSSetShader(scenePixelShader.Get(), nullptr, 0);

        ID3D11Buffer* frameCb = frameBuffer.Get();
        ID3D11Buffer* objectCb = objectBuffer.Get();
        ID3D11Buffer* parameterCb = parameterBuffer.Get();
        context->VSSetConstantBuffers(MiaoShaderContract::kFrameCBufferRegister, 1, &frameCb);
        context->VSSetConstantBuffers(MiaoShaderContract::kObjectCBufferRegister, 1, &objectCb);
        context->VSSetConstantBuffers(MiaoShaderContract::kParameterCBufferRegister, 1, &parameterCb);
        context->PSSetConstantBuffers(MiaoShaderContract::kFrameCBufferRegister, 1, &frameCb);
        context->PSSetConstantBuffers(MiaoShaderContract::kObjectCBufferRegister, 1, &objectCb);
        context->PSSetConstantBuffers(MiaoShaderContract::kParameterCBufferRegister, 1, &parameterCb);

        ID3D11ShaderResourceView* standardTextures[2]{textureViews[0].Get(), textureViews[1].Get()};
        context->PSSetShaderResources(0, 2, standardTextures);
        ID3D11ShaderResourceView* userTextures[8]{};
        for (std::size_t i = 0; i < 8; ++i) userTextures[i] = textureViews[8 + i].Get();
        context->PSSetShaderResources(MiaoShaderContract::kFirstUserTextureRegister, 8, userTextures);

        ID3D11SamplerState* sampler = linearSampler.Get();
        context->PSSetSamplers(MiaoShaderContract::kLinearSamplerRegister, 1, &sampler);
        const float blendFactor[4]{};
        context->OMSetBlendState(alphaBlend.Get(), blendFactor, 0xFFFFFFFFu);
        return true;
    }

    bool ExecuteScenePass(
        const FrameConstants& frame,
        const ObjectConstants& object,
        const MiaoGpuParameterBlock& parameters,
        std::wstring* error) {
        UnbindShaderResources();
        ID3D11RenderTargetView* target = sceneColor.RenderTargetView();
        context->OMSetRenderTargets(1, &target, nullptr);
        const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
        context->ClearRenderTargetView(target, clear);
        SetViewport(sceneColor.Width(), sceneColor.Height());
        if (!BindSceneState(frame, object, parameters, error)) return false;
        context->Draw(3, 0);
        return true;
    }

    bool ExecuteCompositePass(std::wstring* error) {
        if (!sceneColor.Valid() || !backbufferRenderTargetView)
            return Error(error, L"Miao Scene composite resources are unavailable.");

        // A D3D11 resource cannot be bound as RTV and SRV simultaneously. Explicitly
        // end the SceneColor write phase before making it the composite input.
        UnbindShaderResources();
        context->OMSetRenderTargets(0, nullptr, nullptr);

        ID3D11RenderTargetView* target = backbufferRenderTargetView.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        const float clear[4]{0.0f, 0.0f, 0.0f, 1.0f};
        context->ClearRenderTargetView(target, clear);
        SetViewport(width, height);

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(fullscreenVertexShader.Get(), nullptr, 0);
        context->PSSetShader(compositePixelShader.Get(), nullptr, 0);
        ID3D11ShaderResourceView* sceneInput = sceneColor.ShaderResourceView();
        context->PSSetShaderResources(MiaoShaderContract::kInputTextureRegister, 1, &sceneInput);
        ID3D11SamplerState* sampler = linearSampler.Get();
        context->PSSetSamplers(MiaoShaderContract::kLinearSamplerRegister, 1, &sampler);
        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        context->Draw(3, 0);

        ID3D11ShaderResourceView* nullInput{};
        context->PSSetShaderResources(MiaoShaderContract::kInputTextureRegister, 1, &nullInput);
        return true;
    }

    bool ExecutePresentPass(std::wstring* error) {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        const HRESULT present = swapChain->Present(0, 0);
        if (present == DXGI_ERROR_DEVICE_REMOVED || present == DXGI_ERROR_DEVICE_RESET) {
            const HRESULT reason = device ? device->GetDeviceRemovedReason() : present;
            return Error(error, L"Miao Scene GPU device was removed/reset, HRESULT=" + std::to_wstring(reason));
        }
        if (FAILED(present)) return Error(error, L"Miao Scene D3D11 Present failed.");
        return true;
    }

    bool Draw(float timeSeconds, std::wstring* error) {
        if (!loaded || !context || !swapChain || !backbufferRenderTargetView || !sceneColor.Valid())
            return Error(error, L"Miao Scene D3D11 renderer is not loaded.");

        RECT rect{};
        if (!GetClientRect(window, &rect)) return Error(error, L"Cannot query Miao Scene D3D11 surface size.");
        const auto nextWidth = static_cast<unsigned>(std::max<LONG>(1, rect.right - rect.left));
        const auto nextHeight = static_cast<unsigned>(std::max<LONG>(1, rect.bottom - rect.top));
        if (nextWidth != width || nextHeight != height) {
            if (!Resize(nextWidth, nextHeight, error)) return false;
        }

        if (MiaoSceneRuntimeModel::FindInput(definition, L"input://frame/time")) {
            std::wstring inputError;
            if (!runtime.SetInput(L"input://frame/time", static_cast<double>(timeSeconds), &inputError))
                return Error(error, inputError);
        }

        POINT mouse{};
        GetCursorPos(&mouse);
        ScreenToClient(window, &mouse);
        const float mouseX = static_cast<float>(mouse.x);
        const float mouseY = static_cast<float>(mouse.y);
        const float velocityX = hasPreviousMouse ? mouseX - static_cast<float>(previousMouse.x) : 0.0f;
        const float velocityY = hasPreviousMouse ? mouseY - static_cast<float>(previousMouse.y) : 0.0f;
        previousMouse = mouse;
        hasPreviousMouse = true;

        FrameConstants frame{};
        frame.time = timeSeconds;
        frame.deltaTime = previousTime > 0.0f ? std::max(0.0f, timeSeconds - previousTime) : 0.0f;
        frame.resolution[0] = static_cast<float>(width);
        frame.resolution[1] = static_cast<float>(height);
        frame.mousePosition[0] = mouseX;
        frame.mousePosition[1] = mouseY;
        frame.mouseVelocity[0] = velocityX;
        frame.mouseVelocity[1] = velocityY;
        previousTime = timeSeconds;

        ObjectConstants object{};
        SetIdentity(object.world);
        object.size[0] = static_cast<float>(width);
        object.size[1] = static_cast<float>(height);
        Color4 color{1.0, 1.0, 1.0, 1.0};
        if (const auto* tint = runtime.GetProperty(PropertyAddress{renderable->id, L"tint"}))
            color = ReadColor(tint, color);
        if (!programmable) {
            if (const auto* property = FindMaterialProperty(*material, L"color")) {
                const auto materialColor = ReadColor(&property->defaultValue, Color4{1.0, 1.0, 1.0, 1.0});
                color.r *= materialColor.r;
                color.g *= materialColor.g;
                color.b *= materialColor.b;
                color.a *= materialColor.a;
            }
        }
        object.color[0] = static_cast<float>(color.r);
        object.color[1] = static_cast<float>(color.g);
        object.color[2] = static_cast<float>(color.b);
        object.color[3] = static_cast<float>(color.a);
        object.opacity = static_cast<float>(ReadFloat(
            runtime.GetProperty(PropertyAddress{renderable->id, L"opacity"}), 1.0));

        MiaoGpuParameterBlock parameters{};
        std::wstring parameterError;
        if (!MiaoGpuParameterPacker::Pack(definition, runtime, &parameters, &parameterError))
            return Error(error, parameterError);

        for (const auto passIndex : compiledGraph.passOrder) {
            const auto& pass = renderGraph.passes[passIndex];
            if (!pass.enabled) continue;
            switch (pass.kind) {
                case RenderPassKind::Scene2D:
                case RenderPassKind::Programmable:
                    if (!ExecuteScenePass(frame, object, parameters, error)) return false;
                    break;
                case RenderPassKind::Composite:
                    if (!ExecuteCompositePass(error)) return false;
                    break;
                case RenderPassKind::Present:
                    if (!ExecutePresentPass(error)) return false;
                    break;
                case RenderPassKind::Clear:
                case RenderPassKind::Particle:
                case RenderPassKind::PostProcess:
                    return Error(error, L"Miao Scene render graph contains an unsupported pass kind for M1.");
            }
        }

        UnbindShaderResources();
        runtime.MarkPaintReady();
        lastError.clear();
        if (error) error->clear();
        return true;
    }

    bool Resize(unsigned nextWidth, unsigned nextHeight, std::wstring* error) {
        if (!swapChain || !context) return Error(error, L"Miao Scene D3D11 swap-chain is not available.");
        nextWidth = std::max(1u, nextWidth);
        nextHeight = std::max(1u, nextHeight);

        UnbindShaderResources();
        context->OMSetRenderTargets(0, nullptr, nullptr);
        sceneColor.Reset();
        backbufferRenderTargetView.Reset();

        const HRESULT hr = swapChain->ResizeBuffers(0, nextWidth, nextHeight, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) return Error(error, L"Cannot resize Miao Scene D3D11 swap-chain.");
        width = nextWidth;
        height = nextHeight;
        if (!CreateBackbuffer(error)) return false;
        return CreateSceneColor(error);
    }

    void Reset() noexcept {
        if (context) {
            UnbindShaderResources();
            context->OMSetRenderTargets(0, nullptr, nullptr);
            context->ClearState();
            context->Flush();
        }
        for (auto& view : textureViews) view.Reset();
        sceneColor.Reset();
        alphaBlend.Reset();
        linearSampler.Reset();
        parameterBuffer.Reset();
        objectBuffer.Reset();
        frameBuffer.Reset();
        compositePixelShader.Reset();
        scenePixelShader.Reset();
        sceneVertexShader.Reset();
        fullscreenVertexShader.Reset();
        backbufferRenderTargetView.Reset();
        swapChain.Reset();
        context.Reset();
        device.Reset();
        runtime.Reset();
        assets.Clear();
        definition = {};
        package = {};
        renderable = nullptr;
        material = nullptr;
        renderGraph = {};
        compiledGraph = {};
        lastError.clear();
        width = 0;
        height = 0;
        previousTime = 0.0f;
        previousMouse = {};
        hasPreviousMouse = false;
        programmable = false;
        loaded = false;
        window = nullptr;
    }
};

MiaoSceneD3D11Renderer::MiaoSceneD3D11Renderer() : impl_(std::make_unique<Impl>()) {}
MiaoSceneD3D11Renderer::~MiaoSceneD3D11Renderer() = default;

bool MiaoSceneD3D11Renderer::Load(const fs::path& packageRoot, HWND window, std::wstring* error) {
    return impl_->Load(packageRoot, window, error);
}

bool MiaoSceneD3D11Renderer::Draw(float timeSeconds, std::wstring* error) {
    return impl_->Draw(timeSeconds, error);
}

bool MiaoSceneD3D11Renderer::Resize(unsigned width, unsigned height, std::wstring* error) {
    return impl_->Resize(width, height, error);
}

void MiaoSceneD3D11Renderer::Reset() noexcept { impl_->Reset(); }
bool MiaoSceneD3D11Renderer::Loaded() const noexcept { return impl_->loaded; }
bool MiaoSceneD3D11Renderer::UsesProgrammableMaterial() const noexcept { return impl_->programmable; }

std::wstring MiaoSceneD3D11Renderer::PackageId() const {
    if (!impl_->loaded) return {};
    std::wstring result;
    for (unsigned char ch : impl_->package.manifest.id) result.push_back(static_cast<wchar_t>(ch));
    return result;
}

std::wstring MiaoSceneD3D11Renderer::LastErrorText() const { return impl_->lastError; }

bool MiaoSceneD3D11Renderer::SelfTest() {
    return MiaoRenderGraph::SelfTest() && MiaoShaderContract::SelfTest() &&
           MiaoGpuParameterPacker::SelfTest() && MiaoD3D11TextureLoader::SelfTestPathPolicy() &&
           MiaoD3D11RenderTarget::ValidateDimensions(1920, 1080, nullptr) &&
           !MiaoD3D11RenderTarget::ValidateDimensions(0, 1080, nullptr);
}

} // namespace miaodesk::content
