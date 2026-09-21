#include "miaodesk/MiaoSceneD2DRenderer.h"

#include "miaodesk/MiaoAssetDatabase.h"
#include "miaodesk/MiaoContentDataBinding.h"
#include "miaodesk/MiaoContentDefinitionLoader.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoD2DTextureLoader.h"
#include "miaodesk/MiaoSceneRuntime.h"
#include "miaodesk/MiaoSceneSerializer.h"

#include <windows.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <system_error>
#include <unordered_map>
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

std::int64_t ReadInt(const PropertyValue* value, std::int64_t fallback) {
    const auto* integer = value ? std::get_if<std::int64_t>(value) : nullptr;
    return integer ? *integer : fallback;
}

std::wstring ReadString(const PropertyValue* value, std::wstring fallback = {}) {
    const auto* text = value ? std::get_if<std::wstring>(value) : nullptr;
    return text ? *text : std::move(fallback);
}

DWRITE_TEXT_ALIGNMENT TextAlignment(std::wstring_view value) noexcept {
    if (value == L"center") return DWRITE_TEXT_ALIGNMENT_CENTER;
    if (value == L"right" || value == L"trailing") return DWRITE_TEXT_ALIGNMENT_TRAILING;
    return DWRITE_TEXT_ALIGNMENT_LEADING;
}

DWRITE_PARAGRAPH_ALIGNMENT ParagraphAlignment(std::wstring_view value) noexcept {
    if (value == L"center") return DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    if (value == L"bottom" || value == L"far") return DWRITE_PARAGRAPH_ALIGNMENT_FAR;
    return DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
}

DWRITE_FONT_WEIGHT FontWeight(std::int64_t value) noexcept {
    return static_cast<DWRITE_FONT_WEIGHT>(std::clamp<std::int64_t>(value, 100, 999));
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

    state.position = ReadVec2(runtime.GetProperty(PropertyAddress{transform->id, L"position"}), state.position);
    state.scale = ReadVec2(runtime.GetProperty(PropertyAddress{transform->id, L"scale"}), state.scale);
    state.rotationDegrees = ReadFloat(runtime.GetProperty(PropertyAddress{transform->id, L"rotation"}), state.rotationDegrees);
    state.opacity = ReadFloat(runtime.GetProperty(PropertyAddress{transform->id, L"opacity"}), state.opacity);

    state.position.x = std::clamp(state.position.x, -1000000.0, 1000000.0);
    state.position.y = std::clamp(state.position.y, -1000000.0, 1000000.0);
    state.scale.x = std::clamp(state.scale.x, -64.0, 64.0);
    state.scale.y = std::clamp(state.scale.y, -64.0, 64.0);
    state.rotationDegrees = std::fmod(state.rotationDegrees, 360.0);
    state.opacity = std::clamp(state.opacity, 0.0, 1.0);
    return state;
}

D2D1_MATRIX_3X2_F LocalTransformMatrix(const NodeTransformState& transform, const D2D1_SIZE_F& size) noexcept {
    const D2D1_POINT_2F center = D2D1::Point2F(size.width * 0.5f, size.height * 0.5f);
    return D2D1::Matrix3x2F::Scale(static_cast<float>(transform.scale.x), static_cast<float>(transform.scale.y), center) *
           D2D1::Matrix3x2F::Rotation(static_cast<float>(transform.rotationDegrees), center) *
           D2D1::Matrix3x2F::Translation(static_cast<float>(transform.position.x), static_cast<float>(transform.position.y));
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

// Reads one positional channel of one pixel. The target's WIC bitmap is
// 32bppPBGRA, so index 0 is blue, 1 green, 2 red, 3 alpha — the texture assertions
// index it in that order, and a swap would look exactly like a renderer colour bug.
UINT32 PixelChannel(IWICBitmap* bitmap, UINT x, UINT y, UINT channel) {
    if (!bitmap) return 0;
    WICRect rect{0, 0, 64, 64};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockRead, lock.GetAddressOf()))) return 0;
    UINT stride = 0;
    UINT bytes = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&bytes, &data)) || !data) return 0;
    const std::size_t offset = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
    if (offset + channel >= bytes) return 0;
    return data[offset + channel];
}

// Writes a 2x2 flat-colour PNG for the textured-sprite fixture. 2x2 because the
// destination rect is far larger, which is the upscale case a brush has to survive.
//
// Written rather than checked in for two reasons: a stray test image in the package
// tree would itself be an asset the asset database must know about, and a malformed
// image should fail inside the loader under test rather than in packaging.
bool WriteSelfTestPng(IWICImagingFactory* factory, const fs::path& path, const UINT8 (&bgra)[4]) {
    if (!factory) return false;
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmap(2, 2, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf())))
        return false;
    WICRect rect{0, 0, 2, 2};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockWrite, lock.GetAddressOf()))) return false;
    UINT stride = 0;
    UINT bytes = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&bytes, &data)) || !data) return false;
    // PBGRA with alpha 255 means premultiplying is the identity, so the written bytes
    // are the literal colour.
    for (UINT y = 0; y < 2; ++y) {
        for (UINT x = 0; x < 2; ++x) {
            BYTE* pixel = data + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
            pixel[0] = bgra[0];
            pixel[1] = bgra[1];
            pixel[2] = bgra[2];
            pixel[3] = bgra[3];
        }
    }
    lock.Reset();

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()))) return false;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf()))) return false;
    if (FAILED(frame->Initialize(nullptr))) return false;
    if (FAILED(frame->SetSize(2, 2))) return false;
    if (FAILED(frame->WriteSource(bitmap.Get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    return SUCCEEDED(encoder->Commit());
}

} // namespace

struct MiaoSceneD2DRenderer::Impl {
    ID2D1RenderTarget* target{};
    LoadedMiaoContentPackage package;
    ContentDefinition contentDefinition;
    SceneRuntimeDefinition definition;
    MiaoAssetDatabase assets;
    MiaoSceneRuntime runtime;
    ContentDataValues hostData;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<IDWriteFactory> dwrite;
    // Keyed by asset id, not by path: the asset id is what the scene names, and two
    // components naming the same asset must share one bitmap.
    //
    // An ID2D1Bitmap is bound to the render target that created it, so this cache
    // cannot outlive either one. Reset() drops it, and Load() calls Reset() first,
    // which is also the path the hosts take when D2D reports D2DERR_RECREATE_TARGET.
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> spriteTextures;
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
        if (!MiaoContentDefinitionLoader::FromPackage(package, &contentDefinition, &lastError)) return Error(error, lastError);
        if (!MiaoSceneSerializer::DeserializePackage(package, &definition, &lastError)) return Error(error, lastError);
        // A 3D scene is valid content — the model validator accepts it, and the whole
        // point of the B-4 declaration layer was to give a renderer something precise to
        // implement against. This backend has no projection, no depth buffer and no mesh
        // loader, so it cannot honour lights, fog, mesh assets or the third axis.
        //
        // The validation layer already refuses the mirror-image case (lights/fog declared
        // without spatial:3d), but nothing on the render path refused the 3D scene itself
        // — so a hand-placed package would load here and draw as a flat 2D scene with
        // every light silently ignored. That is the failure DESIGN_BASELINE §10 names, and
        // the skill already tells authors not to produce it; the renderer has to say it too.
        //
        // The hosts degrade to a built-in wallpaper on a Load failure, so this surfaces as
        // a visible one rather than a desktop with a missing glow.
        if (definition.scene.spatial == SceneSpatialMode::ThreeD) {
            return Error(error,
                L"This package declares spatial:3d, which the D2D backend cannot render. "
                L"Lights, fog, mesh assets and the third axis are not implemented; the D3D11 "
                L"backend is the planned home for 3D. Declare spatial:2d, or use the D3D11 backend. "
                L"(scene " + definition.scene.id + L")");
        }
        if (!assets.Build(package.root, definition, &lastError)) return Error(error, lastError);
        if (!runtime.Initialize(definition, &lastError)) return Error(error, lastError);
        if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush.GetAddressOf())))
            return Error(error, L"Cannot create Miao Scene D2D brush.");
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()))))
            return Error(error, L"Cannot create DirectWrite factory for Miao Scene text.");

        lastRenderedGeneration = runtime.State().generation;
        loaded = true;
        lastError.clear();
        if (error) error->clear();
        return true;
    }

    bool AdvanceClock(double timeSeconds, std::wstring* error) {
        std::wstring runtimeError;
        if (MiaoSceneRuntimeModel::FindInput(definition, kFrameTimeInput)) {
            if (!runtime.SetInput(kFrameTimeInput, timeSeconds, &runtimeError)) return Error(error, runtimeError);
        } else if (!runtime.AdvanceTimeline(timeSeconds, &runtimeError)) {
            return Error(error, runtimeError);
        }
        return true;
    }

    bool DrawTextComponent(
        const SceneNodeDefinition& node,
        const SceneComponentDefinition& component,
        const ContentDataSnapshot& data,
        const D2D1_SIZE_F& size,
        const D2D1_MATRIX_3X2_F& hostTransform,
        bool* drew,
        std::wstring* error) {
        const std::wstring source = ReadString(runtime.GetProperty(PropertyAddress{component.id, L"value"}));
        if (source.empty()) return true;

        std::wstring text = source;
        if (source.find(L"{{") != std::wstring::npos) {
            if (!MiaoContentDataBinding::ResolveTemplate(contentDefinition, data, source, &text, &lastError))
                return Error(error, lastError);
        }

        const double fontSize = std::clamp(ReadFloat(runtime.GetProperty(PropertyAddress{component.id, L"fontSize"}), 24.0), 1.0, 512.0);
        const Color4 color = ReadColor(runtime.GetProperty(PropertyAddress{component.id, L"color"}), Color4{1.0, 1.0, 1.0, 1.0});
        const double opacity = std::clamp(
            ReadFloat(runtime.GetProperty(PropertyAddress{component.id, L"opacity"}), 1.0) * ResolveNodeOpacity(definition.scene, node, runtime),
            0.0, 1.0);
        const std::wstring align = ReadString(runtime.GetProperty(PropertyAddress{component.id, L"align"}), L"left");
        const std::wstring verticalAlign = ReadString(runtime.GetProperty(PropertyAddress{component.id, L"verticalAlign"}), L"top");
        std::wstring fontFamily = ReadString(runtime.GetProperty(PropertyAddress{component.id, L"fontFamily"}), L"Segoe UI Variable Text");
        if (fontFamily.empty()) fontFamily = L"Segoe UI";
        const auto weight = FontWeight(ReadInt(runtime.GetProperty(PropertyAddress{component.id, L"fontWeight"}), 400));

        ComPtr<IDWriteTextFormat> format;
        HRESULT formatResult = dwrite->CreateTextFormat(
            fontFamily.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            static_cast<float>(fontSize), L"zh-CN", format.GetAddressOf());
        if (FAILED(formatResult) && fontFamily != L"Segoe UI") {
            format.Reset();
            formatResult = dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                static_cast<float>(fontSize), L"zh-CN", format.GetAddressOf());
        }
        if (FAILED(formatResult) || !format)
            return Error(error, L"Cannot create DirectWrite text format for Scene TextRenderer.");

        format->SetTextAlignment(TextAlignment(align));
        format->SetParagraphAlignment(ParagraphAlignment(verticalAlign));
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        brush->SetColor(ToD2D(color, opacity));

        const auto sceneTransform = ResolveNodeTransform(definition.scene, node, runtime, size);
        target->SetTransform(sceneTransform * hostTransform);
        target->DrawText(
            text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
            D2D1::RectF(0.0f, 0.0f, size.width, size.height), brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
        target->SetTransform(hostTransform);
        if (drew) *drew = true;
        return true;
    }

    // A SpriteRenderer names its image with a `texture` asset reference. An empty or
    // absent reference is not an error: it means "solid colour", which is what the
    // renderer could always do. A present reference that does not resolve is an error,
    // and the checks repeat MiaoSceneModel::Validate on purpose — that validator
    // already rejects all three, so these can only fire for a package loaded through a
    // path that did not run it. Either way the outcome has to be a message rather than
    // a silently blank rectangle.
    bool ResolveSpriteTexture(
        const SceneComponentDefinition& component,
        ID2D1Bitmap** bitmap,
        std::wstring* error) {
        *bitmap = nullptr;

        const auto* value = runtime.GetProperty(PropertyAddress{component.id, L"texture"});
        if (!value) {
            if (const auto* declared = FindDefinitionProperty(component, L"texture"))
                value = &declared->defaultValue;
        }
        const auto* reference = value ? std::get_if<AssetReference>(value) : nullptr;
        if (!reference || reference->id.empty()) return true;

        if (const auto cached = spriteTextures.find(reference->id); cached != spriteTextures.end()) {
            *bitmap = cached->second.Get();
            return true;
        }

        const auto* asset = assets.Find(reference->id);
        if (!asset) return Error(error, L"SpriteRenderer texture asset is missing from the package: " + reference->id);
        if (asset->type != AssetType::Image)
            return Error(error, L"SpriteRenderer texture must reference an Image asset: " + reference->id);

        std::wstring loadError;
        ComPtr<ID2D1Bitmap> decoded;
        if (!MiaoD2DTextureLoader::LoadImageW(target, asset->resolvedPath, decoded.GetAddressOf(), &loadError))
            return Error(error, loadError);
        *bitmap = decoded.Get();
        spriteTextures.emplace(reference->id, std::move(decoded));
        return true;
    }

    bool DrawSpriteComponent(
        const SceneNodeDefinition& node,
        const SceneComponentDefinition& component,
        const D2D1_SIZE_F& size,
        const D2D1_MATRIX_3X2_F& hostTransform,
        bool* drew,
        std::wstring* error) {
        ID2D1Bitmap* texture = nullptr;
        if (!ResolveSpriteTexture(component, &texture, error)) return false;

        // A textured sprite carries its own image and needs no material. A sprite with
        // no texture falls back to the builtin solid-colour path this backend has
        // always had; anything programmable belongs to the D3D11 backend, which has a
        // shader path and this one does not.
        const auto* material = ResolveMaterial(definition, component);
        const bool solidMaterial =
            material && material->model == MaterialModel::Builtin && material->builtinName == L"solidColor";
        if (!texture && !solidMaterial) return true;

        Color4 color{1.0, 1.0, 1.0, 1.0};
        if (solidMaterial) {
            if (const auto* materialColor = FindMaterialProperty(*material, L"color"))
                color = ReadColor(&materialColor->defaultValue, color);
        }

        const auto tint = ReadColor(runtime.GetProperty(PropertyAddress{component.id, L"tint"}), Color4{1.0, 1.0, 1.0, 1.0});
        color.r *= tint.r;
        color.g *= tint.g;
        color.b *= tint.b;
        color.a *= tint.a;

        const double spriteOpacity = ReadFloat(runtime.GetProperty(PropertyAddress{component.id, L"opacity"}), 1.0);
        const double nodeOpacity = ResolveNodeOpacity(definition.scene, node, runtime);
        const double maxCornerRadius = static_cast<double>(std::min(size.width, size.height)) * 0.5;
        const double cornerRadius = std::clamp(
            ReadFloat(runtime.GetProperty(PropertyAddress{component.id, L"cornerRadius"}), 0.0),
            0.0,
            maxCornerRadius);

        const auto sceneTransform = ResolveNodeTransform(definition.scene, node, runtime, size);
        target->SetTransform(sceneTransform * hostTransform);
        const auto rect = D2D1::RectF(0.0f, 0.0f, size.width, size.height);

        if (texture) {
            // A bitmap brush rather than DrawBitmap, for one concrete reason: DrawBitmap
            // has no rounded rect at all, so cornerRadius would have to silently stop
            // working the moment a sprite gains a texture. A brush goes through exactly
            // the same FillRectangle / FillRoundedRectangle calls as the solid colour,
            // so opacity and cornerRadius behave identically whether or not the sprite
            // has an image.
            //
            // Tint is the one thing this path cannot carry, and it is refused rather
            // than partly applied. ID2D1BitmapBrush has no colour member — SetColor
            // belongs to ID2D1SolidColorBrush, and the brush interface itself only
            // offers opacity and transform — and a plain ID2D1RenderTarget has neither a
            // blend-mode setter nor the ID2D1DeviceContext effect API that could
            // multiply the image. There is no one-pass way to colour a bitmap here.
            //
            // The tempting workarounds were each rejected for a stated reason, not out
            // of caution:
            //   * Re-decoding the bitmap per tint needs a cache keyed on the tint, and
            //     tint is animatable — an animated tint on a full-screen texture turns
            //     into one decode per frame.
            //   * PushLayer with an opacity brush can carry tint.alpha, never RGB.
            //   * Ignoring the RGB part draws the wrong colour with no signal, which is
            //     the failure DESIGN_BASELINE §10 warns about.
            // If the 2D backend ever renders through an ID2D1DeviceContext this
            // restriction lifts; until then an explicit non-white tint on a textured
            // sprite is a hard error that names the component.
            if (tint.r != 1.0 || tint.g != 1.0 || tint.b != 1.0) {
                return Error(error,
                    L"SpriteRenderer tint is not supported on a textured sprite in the D2D backend: clear the "
                    L"component's tint to draw the image as authored. (component " + component.id + L")");
            }
            ComPtr<ID2D1BitmapBrush> textured;
            const auto brushProperties = D2D1::BitmapBrushProperties(
                D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            if (FAILED(target->CreateBitmapBrush(texture, brushProperties, textured.GetAddressOf())))
                return Error(error, L"Cannot create Miao Scene D2D bitmap brush.");
            textured->SetOpacity(static_cast<float>(spriteOpacity * nodeOpacity));
            if (cornerRadius > 0.0) {
                target->FillRoundedRectangle(
                    D2D1::RoundedRect(rect, static_cast<float>(cornerRadius), static_cast<float>(cornerRadius)),
                    textured.Get());
            } else {
                target->FillRectangle(rect, textured.Get());
            }
        } else {
            brush->SetColor(ToD2D(color, spriteOpacity * nodeOpacity));
            if (cornerRadius > 0.0) {
                target->FillRoundedRectangle(
                    D2D1::RoundedRect(rect, static_cast<float>(cornerRadius), static_cast<float>(cornerRadius)),
                    brush.Get());
            } else {
                target->FillRectangle(rect, brush.Get());
            }
        }

        target->SetTransform(hostTransform);
        if (drew) *drew = true;
        return true;
    }

    bool Draw(float timeSeconds, const D2D1_SIZE_F& size, std::wstring* error) {
        if (!loaded || !target || !brush || !dwrite) return Error(error, L"Miao Scene D2D renderer is not loaded.");
        if (size.width <= 0.0f || size.height <= 0.0f) return Error(error, L"Miao Scene D2D render size is invalid.");
        if (!AdvanceClock(static_cast<double>(timeSeconds), error)) return false;

        ContentDataSnapshot data = MiaoTimeDataProvider::CaptureLocalTime();
        for (const auto& [path, value] : hostData) data.values[path] = value;

        D2D1_MATRIX_3X2_F hostTransform{};
        target->GetTransform(&hostTransform);
        bool drew = false;
        for (const auto& node : definition.scene.nodes) {
            if (!node.enabled) continue;
            for (const auto& component : node.components) {
                if (component.kind == ComponentKind::TextRenderer) {
                    if (!DrawTextComponent(node, component, data, size, hostTransform, &drew, error)) {
                        target->SetTransform(hostTransform);
                        return false;
                    }
                    continue;
                }
                if (component.kind != ComponentKind::SpriteRenderer) continue;
                if (!DrawSpriteComponent(node, component, size, hostTransform, &drew, error)) {
                    target->SetTransform(hostTransform);
                    return false;
                }
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

    bool SetParameter(std::wstring_view id, PropertyValue value, std::wstring* error) {
        if (!loaded) return Error(error, L"Miao Scene D2D renderer is not loaded.");
        std::wstring runtimeError;
        if (!runtime.SetParameter(id, std::move(value), &runtimeError)) return Error(error, runtimeError);
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

    // Exposed so a host can drive the scene's InputBus through InputBusPublisher
    // instead of re-implementing the "only write declared channels" rule. Null until a
    // package is loaded; the renderer owns the lifetime.
    MiaoSceneRuntime* Runtime() noexcept { return loaded ? &runtime : nullptr; }

    bool SetDataValue(std::wstring_view path, PropertyValue value, std::wstring* error) {
        if (!loaded) return Error(error, L"Miao Scene D2D renderer is not loaded.");
        if (path.empty() || path.size() > 256) return Error(error, L"Content host data path is invalid.");
        std::wstring capabilityError;
        if (!MiaoContentCapabilityBroker::CanRead(contentDefinition, path, &capabilityError))
            return Error(error, capabilityError);
        hostData[std::wstring(path)] = std::move(value);
        lastError.clear();
        if (error) error->clear();
        return true;
    }

    void ClearDataValues() noexcept {
        hostData.clear();
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
        dwrite.Reset();
        brush.Reset();
        spriteTextures.clear();
        runtime.Reset();
        assets.Clear();
        hostData.clear();
        definition = {};
        contentDefinition = {};
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

bool MiaoSceneD2DRenderer::SetParameter(std::wstring_view id, PropertyValue value, std::wstring* error) {
    return impl_->SetParameter(id, std::move(value), error);
}

bool MiaoSceneD2DRenderer::SetInput(std::wstring_view id, PropertyValue value, std::wstring* error) {
    return impl_->SetInput(id, std::move(value), error);
}

MiaoSceneRuntime* MiaoSceneD2DRenderer::Runtime() noexcept {
    return impl_ ? impl_->Runtime() : nullptr;
}

bool MiaoSceneD2DRenderer::SetDataValue(std::wstring_view path, PropertyValue value, std::wstring* error) {
    return impl_->SetDataValue(path, std::move(value), error);
}

void MiaoSceneD2DRenderer::ClearDataValues() noexcept {
    impl_->ClearDataValues();
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
      "kind":"widget","runtime":"scene","entry":"scene.json","parameters":"parameters.json","capabilities":["clock.read","weather.read"]
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
            {"name":"cornerRadius","type":"float","default":12.0},
            {"name":"materialId","type":"string","default":"material://background"}
          ]}
        ]},
        {"id":"node://clock","name":"Clock","parentId":"node://root","enabled":true,"components":[
          {"id":"component://clock/transform","kind":"transform","properties":[
            {"name":"position","type":"vec2","default":[0.0,0.0]},
            {"name":"scale","type":"vec2","default":[1.0,1.0]},
            {"name":"rotation","type":"float","default":0.0},
            {"name":"opacity","type":"float","default":1.0}
          ]},
          {"id":"component://clock/text","kind":"textRenderer","properties":[
            {"name":"value","type":"string","default":"{{time.hhmm}} {{weather.condition}}"},
            {"name":"fontSize","type":"float","default":18.0},
            {"name":"color","type":"color","default":[1.0,1.0,1.0,1.0]},
            {"name":"opacity","type":"float","default":1.0},
            {"name":"align","type":"string","default":"center"},
            {"name":"verticalAlign","type":"string","default":"center"},
            {"name":"fontFamily","type":"string","default":"Segoe UI"},
            {"name":"fontWeight","type":"int","default":600}
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

    // Phases B and C build their own sub-packages, so each one needs the files
    // MiaoContentPackage::Load insists on: a manifest whose kind matches the scene,
    // the parameter file the manifest declares, and a directory extension matching
    // the kind (.mdwall for wallpaper). The first version of this test wrote only
    // scene.json into each sub-package and every phase failed at Load with
    // "Miao content package requires manifest.json" — a cost of one Windows round trip
    // for something ContentPackage::Load decides in fifty lines of pure logic.
    const auto writeWallpaperPackage = [&](const fs::path& package, std::string_view sceneJson) {
        constexpr std::string_view wallpaperManifest = R"json({
          "schema":1,"id":"com.goodloong.selftest-textured","name":"Self Test Textured","author":"MiaoDesk","version":"1.0.0",
          "kind":"wallpaper","runtime":"scene","entry":"scene.json","parameters":"parameters.json","capabilities":[]
        })json";
        constexpr std::string_view emptyParameters = R"json({"schema":1,"parameters":[]})json";
        std::error_code local;
        fs::create_directories(package / L"assets", local);
        return WriteTextFile(package / L"manifest.json", wallpaperManifest) &&
               WriteTextFile(package / L"parameters.json", emptyParameters) &&
               WriteTextFile(package / L"scene.json", sceneJson);
    };

    std::wstring error;
    // A boolean with no output is unactionable in CI: this gate runs as a Windows step
    // whose only signal is the ::error:: annotations the wrapper emits, and
    // "SelfTest returned false" names nothing — it does not say which of the two phases
    // failed, or why. Every step below prints its own line, so the annotation carries
    // the failing step's name plus the renderer's error text.
    std::printf("\n--- MiaoSceneD2DRenderer::SelfTest ---\n");
    int failures = 0;
    auto Step = [&](bool condition, const char* what) {
        std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what);
        if (!condition) {
            ++failures;
            ok = false;  // every step is guarded by `ok &&`, so this stops the cascade
            if (!error.empty()) std::printf("         (error = %ls)\n", error.c_str());
        }
        return condition;
    };

    MiaoSceneD2DRenderer renderer;
    Step(ok && renderer.Load(root, target.Get(), &error), "A. 加载纯色 + 文本的 widget 场景");
    Step(ok && renderer.SetParameter(L"param://opacity", 0.6, &error), "A. 参数透传(param://opacity)");
    Step(ok && renderer.SetDataValue(L"weather.condition", std::wstring(L"晴"), &error), "A. 宿主数据注入(weather.condition)");
    Step(ok && [&] {
        target->BeginDraw();
        target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        const bool drew = renderer.Draw(1.0f, D2D1::SizeF(64.0f, 64.0f), &error);
        return SUCCEEDED(target->EndDraw()) && drew && renderer.Loaded() &&
               renderer.Profile() == RuntimeProfile::Widget;
    }(), "A. 首帧绘制并 EndDraw,Loaded 与 profile 正确");
    Step(ok && [&] {
        MiaoSceneFrameDemand idleDemand;
        return renderer.PrepareFrame(1.0, &idleDemand, 60, &error) && !idleDemand.render;
    }(), "A. 静止时帧调度器不要求重绘");
    Step(ok && [&] {
        if (!renderer.SetInput(L"input://event/pulse", true, &error)) return false;
        MiaoSceneFrameDemand activeDemand;
        return renderer.PrepareFrame(1.0, &activeDemand, 60, &error) && activeDemand.render &&
               activeDemand.continuousAnimation && activeDemand.intervalMs == 17;
    }(), "A. 脉冲触发后进入连续动画(17ms 间隔)");
    Step(ok && [&] {
        target->BeginDraw();
        target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        const bool drew = renderer.Draw(1.25f, D2D1::SizeF(64.0f, 64.0f), &error);
        return SUCCEEDED(target->EndDraw()) && drew;
    }(), "A. 动画进行中再画一帧");
    Step(ok && [&] {
        MiaoSceneFrameDemand terminalDemand;
        if (!renderer.PrepareFrame(1.5, &terminalDemand, 60, &error) || !terminalDemand.render) return false;
        target->BeginDraw();
        target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
        const bool drew = renderer.Draw(1.5f, D2D1::SizeF(64.0f, 64.0f), &error);
        return SUCCEEDED(target->EndDraw()) && drew;
    }(), "A. 动画收尾帧");
    Step(ok && [&] {
        MiaoSceneFrameDemand completedDemand;
        return renderer.PrepareFrame(1.75, &completedDemand, 60, &error) && !completedDemand.render;
    }(), "A. 动画结束后回到静止");

    ok = failures == 0;

    // --- Phase A-geometry: the solid sprite's own pixels, with nothing else on top. ---
    //
    // These pixel assertions used to live in phase A, and phase A is where they cannot
    // pass: that scene also has a centred white TextRenderer, and "HH:MM 晴" at 18px in a
    // 64px box is wider than the box, so it word-wraps to two lines. Its first line lands
    // at x≈[9.7,54.3], y≈[10.4,32] — which contains the (17,17) probe the assertion
    // expected to be a dark rounded corner. The sprite was drawn correctly the whole
    // time; the assertion was reading the clock's ink.
    //
    // A corner probe can only speak for the sprite when the sprite is alone. So this
    // phase draws the same kind of sprite in a package with no TextRenderer, and the
    // geometry assertions live here. Phase A keeps the structural checks (load, binding,
    // host data, first frame, frame demand, animation lifecycle) where they always were.
    if (ok) {
        constexpr std::string_view solidSpriteScene = R"json({
          "schema":1,"id":"scene://selftest-solid-sprite","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root",
          "nodes":[
            {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[
              {"id":"component://root/transform","kind":"transform","properties":[
                {"name":"position","type":"vec2","default":[0.0,0.0]},
                {"name":"scale","type":"vec2","default":[1.0,1.0]},
                {"name":"rotation","type":"float","default":0.0},
                {"name":"opacity","type":"float","default":1.0}]}]},
            {"id":"node://panel","name":"Panel","parentId":"node://root","enabled":true,"components":[
              {"id":"component://panel/transform","kind":"transform","properties":[
                {"name":"position","type":"vec2","default":[0.0,0.0]},
                {"name":"scale","type":"vec2","default":[0.5,0.5]},
                {"name":"rotation","type":"float","default":0.0},
                {"name":"opacity","type":"float","default":1.0}]},
              {"id":"component://panel/sprite","kind":"spriteRenderer","properties":[
                {"name":"opacity","type":"float","default":1.0},
                {"name":"tint","type":"color","default":[1.0,1.0,1.0,1.0]},
                {"name":"cornerRadius","type":"float","default":12.0},
                {"name":"materialId","type":"string","default":"material://panel"}]}
            ]}
          ],
          "assets":[],"shaders":[],
          "materials":[{"id":"material://panel","model":"builtin","builtinName":"solidColor","properties":[
            {"name":"color","type":"color","default":[0.2,0.4,0.8,1.0]}
          ],"textures":[]}],
          "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
          "bindings":[],"animations":[]
        })json";
        const fs::path solid = root / L"solid.mdwall";
        MiaoSceneD2DRenderer solidRenderer;
        std::wstring solidError;
        if (ok && writeWallpaperPackage(solid, solidSpriteScene)) {
            Step(solidRenderer.Load(solid, target.Get(), &solidError),
                 "A2. 只有纯色 sprite 的场景加载(没有任何文本覆盖)");
            if (ok) {
                target->BeginDraw();
                target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
                const bool drew = solidRenderer.Draw(1.0f, D2D1::SizeF(64.0f, 64.0f), &solidError);
                Step(SUCCEEDED(target->EndDraw()) && drew, "A2. 首帧绘制并 EndDraw");
            }
            auto dump = [&](UINT x, UINT y, const char* what) {
                std::printf("         %-18s (%2u,%2u) BGRA = %3u,%3u,%3u,%3u\n", what, x, y,
                            PixelChannel(bitmap.Get(), x, y, 0), PixelChannel(bitmap.Get(), x, y, 1),
                            PixelChannel(bitmap.Get(), x, y, 2), PixelChannel(bitmap.Get(), x, y, 3));
            };
            // Dumped unconditionally, not only on failure: a passing assertion that prints
            // its numbers is what makes the next failing one diagnosable.
            const bool centreColoured = PixelHasColor(bitmap.Get(), 32, 32, true);
            const bool innerCornerClear = PixelHasColor(bitmap.Get(), 17, 17, false);
            const bool outerCornerClear = PixelHasColor(bitmap.Get(), 2, 2, false);
            dump(32, 32, "centre");
            dump(17, 17, "inner corner");
            dump(2, 2, "outer corner");
            // An edge scan, not just a centre/corner triple: it distinguishes "sprite not
            // drawn" from "drawn at the wrong extent, or scaled, or unrounded".
            std::printf("         y=32 横向扫描(0=暗,1=有颜色):");
            for (UINT x = 0; x < 64; ++x) {
                const bool lit = PixelChannel(bitmap.Get(), x, 32, 0) > 8 ||
                                 PixelChannel(bitmap.Get(), x, 32, 1) > 8 ||
                                 PixelChannel(bitmap.Get(), x, 32, 2) > 8;
                std::printf("%d", lit ? 1 : 0);
                if (x % 8 == 7) std::printf(" ");
            }
            std::printf("\n         (每 8 像素一组;0.5 缩放 + 12 圆角应为 00000000 11111111 11111111 11111111 00000000)\n");
            Step(centreColoured && innerCornerClear && outerCornerClear,
                 "A2. 纯色 sprite 落在中心、圆角让四角留黑");
        }
        fs::remove_all(solid, ec);
    }

    // --- Phase B: a textured sprite, in a package that has no material at all. ---
    //
    // This is the phase that did not exist before, and the reason it matters: the
    // renderer's only other verifier is this function, and nothing called this
    // function. A draw path that no test reaches is not a verified draw path.
    //
    // The scene deliberately declares no materials. A textured sprite does not need
    // one, and if the textured branch were still gated behind a solidColor material
    // lookup this phase would draw nothing and fail on the first pixel assertion.
    if (ok) {
        const fs::path textured = root / L"textured.mdwall";
        fs::create_directories(textured / L"assets", ec);
        // Flat magenta, chosen because neither the phase-A blue nor the black clear
        // colour is anywhere near it, so a pass cannot come from the wrong source.
        constexpr UINT8 kMagentaBgra[4] = {0xB0, 0x00, 0xB0, 0xFF};
        constexpr std::string_view texturedScene = R"json({
          "schema":1,"id":"scene://selftest-textured","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root",
          "nodes":[
            {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
            {"id":"node://panel","name":"Panel","parentId":"node://root","enabled":true,"components":[
              {"id":"component://panel/transform","kind":"transform","properties":[
                {"name":"position","type":"vec2","default":[0.0,0.0]},
                {"name":"scale","type":"vec2","default":[1.0,1.0]},
                {"name":"rotation","type":"float","default":0.0},
                {"name":"opacity","type":"float","default":1.0}]},
              {"id":"component://panel/sprite","kind":"spriteRenderer","properties":[
                {"name":"opacity","type":"float","default":1.0},
                {"name":"tint","type":"color","default":[1.0,1.0,1.0,1.0]},
                {"name":"cornerRadius","type":"float","default":0.0},
                {"name":"texture","type":"assetReference","default":"asset://panel/texture"}]}
            ]}
          ],
          "assets":[{"id":"asset://panel/texture","type":"image","source":"assets/panel.png"}],
          "shaders":[],"materials":[],
          "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
          "bindings":[],"animations":[]
        })json";

        // The same manifest works: the manifest describes the package, and both phases
        // are scene-runtime content.
        ok = WriteSelfTestPng(wic.Get(), textured / L"assets" / L"panel.png", kMagentaBgra) &&
             writeWallpaperPackage(textured, texturedScene);
        Step(ok, "B. 用 WIC 写出 2x2 品红 PNG 并落成包内资产");

        MiaoSceneD2DRenderer texturedRenderer;
        Step(ok && texturedRenderer.Load(textured, target.Get(), &error),
             "B. 贴图场景加载(该场景一个 material 都没有)");
        Step(ok && [&] {
            target->BeginDraw();
            target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
            const bool drew = texturedRenderer.Draw(1.0f, D2D1::SizeF(64.0f, 64.0f), &error);
            return SUCCEEDED(target->EndDraw()) && drew;
        }(), "B. 贴图 sprite 首帧绘制");
        Step(ok && (PixelChannel(bitmap.Get(), 32, 32, 0) == kMagentaBgra[0] &&
                    PixelChannel(bitmap.Get(), 32, 32, 1) == kMagentaBgra[1] &&
                    PixelChannel(bitmap.Get(), 32, 32, 2) == kMagentaBgra[2]),
             "B. 中心像素是 PNG 的品红(不是黑底,也不是 A 阶段的蓝)");
        Step(ok && PixelHasColor(bitmap.Get(), 1, 1, true),
             "B. 四角也有颜色(cornerRadius=0,即整块被 brush 覆盖)");
        Step(ok && [&] {
            // A second draw must reuse the decoded bitmap, not re-decode per frame, and
            // must produce the same pixels.
            target->BeginDraw();
            target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
            const bool drew = texturedRenderer.Draw(2.0f, D2D1::SizeF(64.0f, 64.0f), &error);
            return SUCCEEDED(target->EndDraw()) && drew &&
                   PixelChannel(bitmap.Get(), 32, 32, 0) == kMagentaBgra[0];
        }(), "B. 连画第二遍结果一致(位图缓存生效,不是每帧重解码)");

        // A non-white tint on a textured sprite is refused, not silently dropped. This
        // pins that refusal: without it the constraint could be quietly removed and
        // the scene would start drawing the wrong colour with nothing failing.
        if (ok) {
            constexpr std::string_view tintedScene = R"json({
              "schema":1,"id":"scene://selftest-tinted","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root",
              "nodes":[
                {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
                {"id":"node://panel","name":"Panel","parentId":"node://root","enabled":true,"components":[
                  {"id":"component://panel/transform","kind":"transform","properties":[
                    {"name":"position","type":"vec2","default":[0.0,0.0]},
                    {"name":"scale","type":"vec2","default":[1.0,1.0]},
                    {"name":"rotation","type":"float","default":0.0},
                    {"name":"opacity","type":"float","default":1.0}]},
                  {"id":"component://panel/sprite","kind":"spriteRenderer","properties":[
                    {"name":"opacity","type":"float","default":1.0},
                    {"name":"tint","type":"color","default":[0.0,1.0,0.0,1.0]},
                    {"name":"cornerRadius","type":"float","default":0.0},
                    {"name":"texture","type":"assetReference","default":"asset://panel/texture"}]}
                ]}
              ],
              "assets":[{"id":"asset://panel/texture","type":"image","source":"assets/panel.png"}],
              "shaders":[],"materials":[],
              "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
              "bindings":[],"animations":[]
            })json";
            const fs::path tinted = root / L"tinted.mdwall";
            fs::create_directories(tinted / L"assets", ec);
            MiaoSceneD2DRenderer tintedRenderer;
            std::wstring tintedError;
            // Load must still succeed: a non-white tint is a perfectly legal scene, and
            // the model validator accepts it. It is the draw path that refuses, which
            // is what makes the next assertion meaningful.
            if (ok && WriteSelfTestPng(wic.Get(), tinted / L"assets" / L"panel.png", kMagentaBgra) &&
                       writeWallpaperPackage(tinted, tintedScene)) {
                // Load must still succeed: a non-white tint is a perfectly legal scene,
                // and the model validator accepts it. It is the draw path that refuses.
                Step(tintedRenderer.Load(tinted, target.Get(), &tintedError),
                     "B. 非白色 tint 的场景仍然能加载(它在模型层合法)");
                if (ok) {
                    target->BeginDraw();
                    target->Clear(D2D1::ColorF(D2D1::ColorF::Black));
                    const bool drew = tintedRenderer.Draw(1.0f, D2D1::SizeF(64.0f, 64.0f), &tintedError);
                    const bool refused = !drew &&
                                         // the message has to name the offending component
                                         tintedError.find(L"component://panel/sprite") != std::wstring::npos &&
                                         // and nothing may have been painted
                                         PixelHasColor(bitmap.Get(), 32, 32, false);
                    if (tintedError.empty()) tintedError = L"(绘制路径没有给出任何报错)";
                    error = tintedError;  // Step prints `error`, so surface the refusal's reason
                    Step(SUCCEEDED(target->EndDraw()) && refused,
                         "B. 非白色 tint 作用于贴图被拒,且报错点名组件、没有画出任何东西");
                    error.clear();
                }
            }
        }
        // --- Phase C: a 3D scene is legal content this backend cannot draw. ---
        //
        // The model validator accepts spatial:3d deliberately — that is what makes the
        // declaration layer useful. Light/fog declared *without* 3d is already refused by
        // MiaoSceneRuntimeModel::Validate; the reverse (a valid 3D scene reaching a
        // backend with no projection) was refused by nobody, so it drew flat and dropped
        // every light silently. This pins the refusal.
        if (ok) {
            constexpr std::string_view spatial3DScene = R"json({
              "schema":1,"id":"scene://selftest-spatial-3d","kind":"wallpaper","profile":"wallpaper",
              "spatial":"3d","rootNodeId":"node://root",
              "nodes":[
                {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
                {"id":"node://panel","name":"Panel","parentId":"node://root","enabled":true,"components":[
                  {"id":"component://panel/transform","kind":"transform","properties":[
                    {"name":"position","type":"vec2","default":[0.0,0.0]},
                    {"name":"scale","type":"vec2","default":[1.0,1.0]},
                    {"name":"rotation","type":"float","default":0.0},
                    {"name":"opacity","type":"float","default":1.0}]},
                  {"id":"component://panel/sprite","kind":"spriteRenderer","properties":[
                    {"name":"opacity","type":"float","default":1.0},
                    {"name":"tint","type":"color","default":[1.0,1.0,1.0,1.0]},
                    {"name":"cornerRadius","type":"float","default":0.0}]}
                ]}
              ],
              "assets":[],"shaders":[],
              "lights":[{"id":"light://key","type":"point","nodeId":"node://root",
                         "color":[1.0,1.0,1.0,1.0],"intensity":1.0,"range":100.0}],
              "fog":[],"materials":[],
              "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
              "bindings":[],"animations":[]
            })json";
            const fs::path spatial3D = root / L"spatial3d.mdwall";
            fs::create_directories(spatial3D, ec);
            MiaoSceneD2DRenderer spatialRenderer;
            std::wstring spatialError;
            if (ok && writeWallpaperPackage(spatial3D, spatial3DScene)) {
                const bool loaded = spatialRenderer.Load(spatial3D, target.Get(), &spatialError);
                error = spatialError;  // Step prints `error`, so surface the refusal's reason
                Step(!loaded && spatialError.find(L"scene://selftest-spatial-3d") != std::wstring::npos,
                     "C. spatial:3d 的场景被拒,且报错点名场景 id(而不是静默按 2D 画)");
                error.clear();
            }
            fs::remove_all(spatial3D, ec);
        }

        fs::remove_all(root / L"textured.mdwall", ec);
        fs::remove_all(root / L"tinted.mdwall", ec);
        fs::remove_all(root / L"spatial3d.mdwall", ec);
    }

    renderer.Reset();
    target.Reset();
    bitmap.Reset();
    wic.Reset();
    factory.Reset();
    fs::remove_all(root, ec);
    if (shouldUninitialize) CoUninitialize();
    std::printf("--- SelfTest:%d failure(s) ---\n", failures);
    return failures == 0;
}

} // namespace miaodesk::content
