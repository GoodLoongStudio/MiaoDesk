#include "miaodesk/MiaoSceneD2DRenderer.h"

#include "miaodesk/MiaoAssetDatabase.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoSceneRuntime.h"
#include "miaodesk/MiaoSceneSerializer.h"

#include <windows.h>
#include <d2d1helper.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace miaodesk::content {
namespace {

constexpr std::wstring_view kFrameTimeInput = L"input://frame/time";

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

const PropertyDefinition* FindDefinitionProperty(const SceneComponentDefinition& component, std::wstring_view name) {
    for (const auto& property : component.properties) if (property.name == name) return &property;
    return nullptr;
}

const PropertyDefinition* FindMaterialProperty(const MaterialDefinition& material, std::wstring_view name) {
    for (const auto& property : material.properties) if (property.name == name) return &property;
    return nullptr;
}

const SceneComponentDefinition* FindTransform(const SceneNodeDefinition& node) noexcept {
    for (const auto& component : node.components) {
        if (component.kind == ComponentKind::Transform) return &component;
    }
    return nullptr;
}

const MaterialDefinition* ResolveMaterial(
    const SceneRuntimeDefinition& definition,
    const SceneComponentDefinition& component) {
    if (const auto* property = FindDefinitionProperty(component, L"materialId")) {
        if (property->type == PropertyType::String) {
            if (const auto* id = std::get_if<std::wstring>(&property->defaultValue)) {
                if (const auto* material = MiaoSceneRuntimeModel::FindMaterial(definition, *id)) return material;
            }
        }
    }
    for (const auto& material : definition.materials) {
        if (material.model == MaterialModel::Builtin) return &material;
    }
    return nullptr;
}

Color4 ReadColor(const PropertyValue* value, Color4 fallback) {
    const auto* color = value ? std::get_if<Color4>(value) : nullptr;
    return color ? *color : fallback;
}

Vec2 ReadVec2(const PropertyValue* value, Vec2 fallback) {
    const auto* vector = value ? std::get_if<Vec2>(value) : nullptr;
    if (!vector || !std::isfinite(vector->x) || !std::isfinite(vector->y)) return fallback;
    return *vector;
}

double ReadFloat(const PropertyValue* value, double fallback) {
    const auto* number = value ? std::get_if<double>(value) : nullptr;
    return number && std::isfinite(*number) ? *number : fallback;
}

struct NodeTransformState {
    Vec2 position{};
    Vec2 scale{1.0, 1.0};
    double rotationDegrees{};
    double opacity{1.0};
};

NodeTransformState ReadTransformState(
    const SceneNodeDefinition& node,
    const MiaoSceneRuntime& runtime) noexcept {
    NodeTransformState state;
    const auto* transform = FindTransform(node);
    if (!transform) return state;

    state.position = ReadVec2(
        runtime.GetProperty(PropertyAddress{transform->id, L"position"}),
        state.position);
    state.scale = ReadVec2(
        runtime.GetProperty(PropertyAddress{transform->id, L"scale"}),
        state.scale);
    state.rotationDegrees = ReadFloat(
        runtime.GetProperty(PropertyAddress{transform->id, L"rotation"}),
        state.rotationDegrees);
    state.opacity = ReadFloat(
        runtime.GetProperty(PropertyAddress{transform->id, L"opacity"}),
        state.opacity);

    state.position.x = std::clamp(state.position.x, -1000000.0, 1000000.0);
    state.position.y = std::clamp(state.position.y, -1000000.0, 1000000.0);
    state.scale.x = std::clamp(state.scale.x, -64.0, 64.0);
    state.scale.y = std::clamp(state.scale.y, -64.0, 64.0);
    state.rotationDegrees = std::fmod(state.rotationDegrees, 360.0);
    state.opacity = std::clamp(state.opacity, 0.0, 1.0);
    return state;
}

D2D1_MATRIX_3X2_F LocalTransformMatrix(
    const NodeTransformState& transform,
    const D2D1_SIZE_F& size) noexcept {
    const D2D1_POINT_2F center = D2D1::Point2F(size.width * 0.5f, size.height * 0.5f);
    return D2D1::Matrix3x2F::Scale(
               static_cast<float>(transform.scale.x),
               static_cast<float>(transform.scale.y),
               center) *
           D2D1::Matrix3x2F::Rotation(static_cast<float>(transform.rotationDegrees), center) *
           D2D1::Matrix3x2F::Translation(
               static_cast<float>(transform.position.x),
               static_cast<float>(transform.position.y));
}

D2D1_MATRIX_3X2_F ResolveNodeTransform(
    const SceneDefinition& scene,
    const SceneNodeDefinition& node,
    const MiaoSceneRuntime& runtime,
    const D2D1_SIZE_F& size) noexcept {
    D2D1_MATRIX_3X2_F matrix = D2D1::Matrix3x2F::Identity();
    const SceneNodeDefinition* current = &node;
    for (std::size_t depth = 0; current && depth <= scene.nodes.size(); ++depth) {
        matrix = matrix * LocalTransformMatrix(ReadTransformState(*current, runtime), size);
        if (current->parentId.empty()) break;
        current = MiaoSceneModel::FindNode(scene, current->parentId);
    }
    return matrix;
}

double ResolveNodeOpacity(
    const SceneDefinition& scene,
    const SceneNodeDefinition& node,
    const MiaoSceneRuntime& runtime) noexcept {
    double opacity = 1.0;
    const SceneNodeDefinition* current = &node;
    for (std::size_t depth = 0; current && depth <= scene.nodes.size(); ++depth) {
        opacity *= ReadTransformState(*current, runtime).opacity;
        if (current->parentId.empty()) break;
        current = MiaoSceneModel::FindNode(scene, current->parentId);
    }
    return std::clamp(opacity, 0.0, 1.0);
}

D2D1_COLOR_F ToD2D(Color4 color, double opacity = 1.0) {
    auto clamp = [](double value) { return static_cast<float>(std::clamp(value, 0.0, 1.0)); };
    return D2D1::ColorF(clamp(color.r), clamp(color.g), clamp(color.b), clamp(color.a * opacity));
}

bool WriteTextFile(const fs::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
}

bool PixelHasColor(IWICBitmap* bitmap, UINT x, UINT y, bool expectedColor) {
    if (!bitmap) return false;
    WICRect rect{0, 0, 64, 64};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockRead, lock.GetAddressOf()))) return false;
    UINT stride = 0;
    UINT bytes = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&bytes, &data)) || !data) return false;
    const std::size_t offset = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
    if (offset + 3u >= bytes) return false;
    const bool colored = data[offset] > 8 || data[offset + 1] > 8 || data[offset + 2] > 8;
    return colored == expectedColor;
}

} // namespace

struct MiaoSceneD2DRenderer::Impl {
    ID2D1RenderTarget* target{};
    LoadedMiaoContentPackage package;
    SceneRuntimeDefinition definition;
    MiaoAssetDatabase assets;
    MiaoSceneRuntime runtime;
    ComPtr<ID2D1SolidColorBrush> brush;
    std::wstring lastError;
    std::uint64_t lastRenderedGeneration{};
    bool loaded{};

    bool Load(const fs::path& packageRoot, ID2D1RenderTarget* nextTarget, std::wstring* error) {
        Reset();
        if (!nextTarget) return Error(error, L"Miao Scene D2D target is null.");
        target = nextTarget;

        if (!MiaoContentPackage::Load(packageRoot, &package, &lastError)) return Error(error, lastError);
        if (package.manifest.runtime != ContentRuntimeKind::Scene)
            return Error(error, L"Miao Scene D2D renderer requires a scene-runtime content package.");
        if (!MiaoSceneSerializer::DeserializePackage(package, &definition, &lastError)) return Error(error, lastError);
        if (!assets.Build(package.root, definition, &lastError)) return Error(error, lastError);
        if (!runtime.Initialize(definition, &lastError)) return Error(error, lastError);
        if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush.GetAddressOf())))
            return Error(error, L"Cannot create Miao Scene D2D brush.");

        lastRenderedGeneration = runtime.State().generation;
        loaded = true;
        lastError.clear();
        if (error) error->clear();
        return true;
    }

    bool AdvanceClock(double timeSeconds, std::wstring* error) {
        std::wstring runtimeError;
        if (MiaoSceneRuntimeModel::FindInput(definition, kFrameTimeInput)) {
            if (!runtime.SetInput(kFrameTimeInput, timeSeconds, &runtimeError))
                return Error(error, runtimeError);
        } else if (!runtime.AdvanceTimeline(timeSeconds, &runtimeError)) {
            return Error(error, runtimeError);
        }
        return true;
    }

    bool Draw(float timeSeconds, const D2D1_SIZE_F& size, std::wstring* error) {
        if (!loaded || !target || !brush) return Error(error, L"Miao Scene D2D renderer is not loaded.");
        if (size.width <= 0.0f || size.height <= 0.0f) return Error(error, L"Miao Scene D2D render size is invalid.");
        if (!AdvanceClock(static_cast<double>(timeSeconds), error)) return false;

        D2D1_MATRIX_3X2_F hostTransform{};
        target->GetTransform(&hostTransform);
        bool drew = false;
        for (const auto& node : definition.scene.nodes) {
            if (!node.enabled) continue;
            for (const auto& component : node.components) {
                if (component.kind != ComponentKind::SpriteRenderer) continue;
                const auto* material = ResolveMaterial(definition, component);
                if (!material || material->model != MaterialModel::Builtin || material->builtinName != L"solidColor") continue;

                Color4 color{1.0, 1.0, 1.0, 1.0};
                if (const auto* materialColor = FindMaterialProperty(*material, L"color"))
                    color = ReadColor(&materialColor->defaultValue, color);

                const auto tint = ReadColor(
                    runtime.GetProperty(PropertyAddress{component.id, L"tint"}),
                    Color4{1.0, 1.0, 1.0, 1.0});
                color.r *= tint.r;
                color.g *= tint.g;
                color.b *= tint.b;
                color.a *= tint.a;

                const double spriteOpacity = ReadFloat(
                    runtime.GetProperty(PropertyAddress{component.id, L"opacity"}),
                    1.0);
                const double nodeOpacity = ResolveNodeOpacity(definition.scene, node, runtime);
                brush->SetColor(ToD2D(color, spriteOpacity * nodeOpacity));

                const auto sceneTransform = ResolveNodeTransform(definition.scene, node, runtime, size);
                target->SetTransform(sceneTransform * hostTransform);
                target->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, size.height), brush.Get());
                target->SetTransform(hostTransform);
                drew = true;
            }
        }
        target->SetTransform(hostTransform);

        if (!drew) return Error(error, L"Scene has no D2D-renderable component in the current MVP backend.");
        runtime.MarkPaintReady();
        lastRenderedGeneration = runtime.State().generation;
        lastError.clear();
        if (error) error->clear();
        return true;
    }

    bool SetInput(std::wstring_view id, PropertyValue value, std::wstring* error) {
        if (!loaded) return Error(error, L"Miao Scene D2D renderer is not loaded.");
        std::wstring runtimeError;
        if (!runtime.SetInput(id, std::move(value), &runtimeError)) return Error(error, runtimeError);
        lastError.clear();
        if (error) error->clear();
        return true;
    }

    bool PrepareFrame(
        double timeSeconds,
        MiaoSceneFrameDemand* demand,
        std::uint32_t animationFps,
        std::wstring* error) {
        if (!loaded) return Error(error, L"Miao Scene D2D renderer is not loaded.");
        std::wstring schedulerError;
        if (!MiaoSceneFrameScheduler::AdvanceAndEvaluate(
                runtime, timeSeconds, lastRenderedGeneration, demand, animationFps, &schedulerError))
            return Error(error, schedulerError);
        lastError.clear();
        if (error) error->clear();
        return true;
    }

    void Reset() noexcept {
        brush.Reset();
        runtime.Reset();
        assets.Clear();
        definition = {};
        package = {};
        target = nullptr;
        loaded = false;
        lastRenderedGeneration = 0;
        lastError.clear();
    }

    bool Error(std::wstring* error, std::wstring message) {
        lastError = std::move(message);
        if (error) *error = lastError;
        return false;
    }
};

MiaoSceneD2DRenderer::MiaoSceneD2DRenderer() : impl_(std::make_unique<Impl>()) {}
MiaoSceneD2DRenderer::~MiaoSceneD2DRenderer() = default;

bool MiaoSceneD2DRenderer::Load(const fs::path& packageRoot, ID2D1RenderTarget* target, std::wstring* error) {
    return impl_->Load(packageRoot, target, error);
}

bool MiaoSceneD2DRenderer::Draw(float timeSeconds, const D2D1_SIZE_F& size, std::wstring* error) {
    return impl_->Draw(timeSeconds, size, error);
}

bool MiaoSceneD2DRenderer::SetInput(std::wstring_view id, PropertyValue value, std::wstring* error) {
    return impl_->SetInput(id, std::move(value), error);
}

bool MiaoSceneD2DRenderer::PrepareFrame(
    double timeSeconds,
    MiaoSceneFrameDemand* demand,
    std::uint32_t animationFps,
    std::wstring* error) {
    return impl_->PrepareFrame(timeSeconds, demand, animationFps, error);
}

void MiaoSceneD2DRenderer::Reset() noexcept { impl_->Reset(); }
bool MiaoSceneD2DRenderer::Loaded() const noexcept { return impl_->loaded; }
RuntimeProfile MiaoSceneD2DRenderer::Profile() const noexcept {
    return impl_->loaded ? impl_->definition.profile : RuntimeProfile::Wallpaper;
}
std::uint64_t MiaoSceneD2DRenderer::RuntimeGeneration() const noexcept {
    return impl_->loaded ? impl_->runtime.State().generation : 0;
}

std::wstring MiaoSceneD2DRenderer::PackageId() const {
    if (!impl_->loaded) return {};
    std::wstring result;
    for (unsigned char ch : impl_->package.manifest.id) result.push_back(static_cast<wchar_t>(ch));
    return result;
}

std::wstring MiaoSceneD2DRenderer::LastErrorText() const { return impl_->lastError; }

bool MiaoSceneD2DRenderer::SelfTest() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return false;

    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-SceneD2D-SelfTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".mdwidget");
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) {
        if (shouldUninitialize) CoUninitialize();
        return false;
    }

    constexpr std::string_view manifest = R"json({
      "schema":1,"id":"com.goodloong.selftest-widget","name":"Self Test Widget","author":"MiaoDesk","version":"1.0.0",
      "kind":"widget","runtime":"scene","entry":"scene.json","parameters":"parameters.json","capabilities":[]
    })json";
    constexpr std::string_view scene = R"json({
      "schema":1,"id":"scene://selftest-widget","kind":"widget","profile":"widget","rootNodeId":"node://root",
      "nodes":[
        {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
        {"id":"node://background","name":"Background","parentId":"node://root","enabled":true,"components":[
          {"id":"component://background/transform","kind":"transform","properties":[
            {"name":"position","type":"vec2","default":[0.0,0.0]},
            {"name":"scale","type":"vec2","default":[0.5,0.5]},
            {"name":"rotation","type":"float","default":0.0},
            {"name":"opacity","type":"float","default":1.0}
          ]},
          {"id":"component://background/sprite","kind":"spriteRenderer","properties":[
            {"name":"opacity","type":"float","default":1.0},
            {"name":"tint","type":"color","default":[1.0,1.0,1.0,1.0]},
            {"name":"materialId","type":"string","default":"material://background"}
          ]}
        ]}
      ],
      "assets":[],"shaders":[],
      "materials":[{"id":"material://background","model":"builtin","builtinName":"solidColor","properties":[
        {"name":"color","type":"color","default":[0.2,0.4,0.8,1.0]}
      ],"textures":[]}],
      "inputs":[
        {"id":"input://frame/time","type":"float","default":0.0},
        {"id":"input://event/pulse","type":"bool","default":false}
      ],
      "bindings":[{"id":"binding://opacity","sourceKind":"parameter","sourceId":"param://opacity",
        "target":{"componentId":"component://background/sprite","propertyName":"opacity"},"scale":1.0,"offset":0.0}],
      "animations":[
        {"id":"animation://move","target":{"componentId":"component://background/transform","propertyName":"position"},
         "enabled":true,"loop":"once","duration":0.5,"trigger":{"mode":"inputRisingEdge","inputId":"input://event/pulse"},
         "keyframes":[{"time":0.0,"value":[0.0,0.0],"easing":"easeOut"},{"time":0.5,"value":[12.0,0.0],"easing":"linear"}]},
        {"id":"animation://scale","target":{"componentId":"component://background/transform","propertyName":"scale"},
         "enabled":true,"loop":"once","duration":0.5,"trigger":{"mode":"inputRisingEdge","inputId":"input://event/pulse"},
         "keyframes":[{"time":0.0,"value":[0.5,0.5],"easing":"easeInOut"},{"time":0.5,"value":[0.75,0.75],"easing":"linear"}]},
        {"id":"animation://rotate","target":{"componentId":"component://background/transform","propertyName":"rotation"},
         "enabled":true,"loop":"once","duration":0.5,"trigger":{"mode":"inputRisingEdge","inputId":"input://event/pulse"},
         "keyframes":[{"time":0.0,"value":0.0,"easing":"easeInOut"},{"time":0.5,"value":20.0,"easing":"linear"}]},
        {"id":"animation://tint","target":{"componentId":"component://background/sprite","propertyName":"tint"},
         "enabled":true,"loop":"once","duration":0.5,"trigger":{"mode":"inputRisingEdge","inputId":"input://event/pulse"},
         "keyframes":[{"time":0.0,"value":[1.0,1.0,1.0,1.0],"easing":"linear"},{"time":0.5,"value":[1.0,0.55,0.55,1.0],"easing":"linear"}]}
      ]
    })json";
    constexpr std::string_view parameters = R"json({"schema":1,"parameters":[{"id":"param://opacity","type":"float","default":0.9}]})json";

    bool ok = WriteTextFile(root / L"manifest.json", manifest) &&
              WriteTextFile(root / L"scene.json", scene) &&
              WriteTextFile(root / L"parameters.json", parameters);

    ComPtr<ID2D1Factory> factory;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> target;
    if (ok) ok = SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
    if (ok) ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())));
    if (ok) ok = SUCCEEDED(wic->CreateBitmap(64, 64, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf()));
    if (ok) ok = SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(), target.GetAddressOf()));

    std::wstring error;
    MiaoSceneD2DRenderer renderer;
    if (ok) ok = renderer.Load(root, target.Get(), &error);
    if (ok) {
        target->BeginDraw();
        target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        ok = renderer.Draw(1.0f, D2D1::SizeF(64.0f, 64.0f), &error);
        ok = SUCCEEDED(target->EndDraw()) && ok && renderer.Loaded() &&
             renderer.Profile() == RuntimeProfile::Widget;
    }
    if (ok) {
        ok = PixelHasColor(bitmap.Get(), 32, 32, true) && PixelHasColor(bitmap.Get(), 2, 2, false);
    }
    if (ok) {
        MiaoSceneFrameDemand idleDemand;
        ok = renderer.PrepareFrame(1.0, &idleDemand, 60, &error) && !idleDemand.render;
    }
    if (ok) {
        ok = renderer.SetInput(L"input://event/pulse", true, &error);
        MiaoSceneFrameDemand activeDemand;
        ok = ok && renderer.PrepareFrame(1.0, &activeDemand, 60, &error) &&
             activeDemand.render && activeDemand.continuousAnimation && activeDemand.intervalMs == 17;
    }
    if (ok) {
        target->BeginDraw();
        target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        ok = renderer.Draw(1.25f, D2D1::SizeF(64.0f, 64.0f), &error);
        ok = SUCCEEDED(target->EndDraw()) && ok;
    }
    if (ok) {
        MiaoSceneFrameDemand terminalDemand;
        ok = renderer.PrepareFrame(1.5, &terminalDemand, 60, &error) && terminalDemand.render;
        if (ok) {
            target->BeginDraw();
            target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
            ok = renderer.Draw(1.5f, D2D1::SizeF(64.0f, 64.0f), &error);
            ok = SUCCEEDED(target->EndDraw()) && ok;
        }
    }
    if (ok) {
        MiaoSceneFrameDemand completedDemand;
        ok = renderer.PrepareFrame(1.75, &completedDemand, 60, &error) && !completedDemand.render;
    }

    renderer.Reset();
    target.Reset();
    bitmap.Reset();
    wic.Reset();
    factory.Reset();
    fs::remove_all(root, ec);
    if (shouldUninitialize) CoUninitialize();
    return ok;
}

} // namespace miaodesk::content
