#pragma once

#include "miaodesk/SceneWallpaperPainter.h"
#include "miaodesk/WallpaperPackage.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace miaodesk::wallpaper::scenes {
namespace layered_scene_detail {

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

constexpr int kMaxLayers = 32;

inline fs::path ModuleDirectory() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return fs::path(std::wstring(buffer.data(), length)).parent_path();
}

inline fs::path LocatePackage(std::wstring_view packageName) {
    const auto moduleDir = ModuleDirectory();
    if (moduleDir.empty()) return {};

    std::error_code ec;
    auto candidate = moduleDir / L"Wallpapers" / std::wstring(packageName);
    if (fs::is_directory(candidate, ec)) return candidate;

    // Developer builds live several directories below the repository root.
    // Walk upward so a plain MiaoDeskWallpaper build can consume source-owned
    // .mdwall resources without an installer or packaging step.
    fs::path current = moduleDir;
    for (int depth = 0; depth < 9 && !current.empty(); ++depth) {
        ec.clear();
        candidate = current / L"assets" / L"wallpapers" / std::wstring(packageName);
        if (fs::is_directory(candidate, ec)) return candidate;
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return {};
}

inline std::wstring ReadText(const fs::path& ini, const wchar_t* section, const wchar_t* key,
                             const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(4096);
    GetPrivateProfileStringW(section, key, fallback, buffer.data(), static_cast<DWORD>(buffer.size()), ini.c_str());
    return buffer.data();
}

inline float ReadFloat(const fs::path& ini, const wchar_t* section, const wchar_t* key, float fallback) {
    wchar_t fallbackText[64]{};
    swprintf_s(fallbackText, L"%.6f", fallback);
    const auto text = ReadText(ini, section, key, fallbackText);
    wchar_t* end = nullptr;
    const float value = std::wcstof(text.c_str(), &end);
    return end == text.c_str() ? fallback : value;
}

inline int ReadInt(const fs::path& ini, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini.c_str()));
}

inline bool InsidePackage(const fs::path& candidate, const fs::path& packageRoot) {
    std::error_code ec;
    auto root = fs::weakly_canonical(packageRoot, ec);
    if (ec) root = fs::absolute(packageRoot, ec).lexically_normal();
    ec.clear();
    auto file = fs::weakly_canonical(candidate, ec);
    if (ec) file = fs::absolute(candidate, ec).lexically_normal();
    auto rootText = root.wstring();
    auto fileText = file.wstring();
    std::transform(rootText.begin(), rootText.end(), rootText.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    std::transform(fileText.begin(), fileText.end(), fileText.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    if (fileText == rootText) return true;
    if (!rootText.empty() && rootText.back() != L'\\' && rootText.back() != L'/') rootText.push_back(fs::path::preferred_separator);
    return fileText.size() >= rootText.size() && fileText.compare(0, rootText.size(), rootText) == 0;
}

inline float LocalHash01(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

inline float LocalWrap01(float value) {
    value = std::fmod(value, 1.0f);
    return value < 0.0f ? value + 1.0f : value;
}

struct Layer {
    std::wstring name;
    fs::path source;
    std::wstring animation;
    float x{};
    float y{};
    float width{};
    float height{};
    float opacity{1.0f};
    float amplitudeX{};
    float amplitudeY{};
    float scaleAmplitude{};
    float rotationAmplitude{};
    float speed{1.0f};
    float phase{};
    float pivotX{0.5f};
    float pivotY{0.5f};
    float blinkInterval{4.8f};
    float blinkDuration{0.16f};
    ComPtr<ID2D1Bitmap> bitmap;
};

class Renderer {
public:
    Renderer(ID2D1RenderTarget* target, fs::path packageRoot)
        : target_(target), packageRoot_(std::move(packageRoot)) {}

    bool Initialize() {
        WallpaperPackageManifest manifest;
        std::wstring error;
        if (!WallpaperPackage::Validate(packageRoot_, &manifest, &error) ||
            manifest.type != WallpaperPackageType::Scene) return false;

        entry_ = packageRoot_ / manifest.entry;
        designWidth_ = std::max(320.0f, ReadFloat(entry_, L"Scene", L"design_width", 1672.0f));
        designHeight_ = std::max(180.0f, ReadFloat(entry_, L"Scene", L"design_height", 941.0f));
        sparkleCount_ = std::clamp(ReadInt(entry_, L"Particles", L"sparkle_count", 22), 0, 96);
        petalCount_ = std::clamp(ReadInt(entry_, L"Particles", L"petal_count", 12), 0, 64);
        particleOpacity_ = std::clamp(ReadFloat(entry_, L"Particles", L"opacity", 0.46f), 0.0f, 1.0f);

        ComPtr<IWICImagingFactory> wic;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(wic.GetAddressOf())))) return false;

        const int count = std::clamp(ReadInt(entry_, L"Scene", L"layer_count", 0), 0, kMaxLayers);
        layers_.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const std::wstring section = L"Layer" + std::to_wstring(index);
            Layer layer;
            layer.name = ReadText(entry_, section.c_str(), L"name", section.c_str());
            layer.animation = ReadText(entry_, section.c_str(), L"animation", L"none");
            layer.x = ReadFloat(entry_, section.c_str(), L"x", 0.0f);
            layer.y = ReadFloat(entry_, section.c_str(), L"y", 0.0f);
            layer.width = ReadFloat(entry_, section.c_str(), L"width", designWidth_);
            layer.height = ReadFloat(entry_, section.c_str(), L"height", designHeight_);
            layer.opacity = std::clamp(ReadFloat(entry_, section.c_str(), L"opacity", 1.0f), 0.0f, 1.0f);
            layer.amplitudeX = ReadFloat(entry_, section.c_str(), L"amplitude_x", 0.0f);
            layer.amplitudeY = ReadFloat(entry_, section.c_str(), L"amplitude_y", 0.0f);
            layer.scaleAmplitude = ReadFloat(entry_, section.c_str(), L"scale_amplitude", 0.0f);
            layer.rotationAmplitude = ReadFloat(entry_, section.c_str(), L"rotation_amplitude", 0.0f);
            layer.speed = ReadFloat(entry_, section.c_str(), L"speed", 1.0f);
            layer.phase = ReadFloat(entry_, section.c_str(), L"phase", 0.0f);
            layer.pivotX = std::clamp(ReadFloat(entry_, section.c_str(), L"pivot_x", 0.5f), 0.0f, 1.0f);
            layer.pivotY = std::clamp(ReadFloat(entry_, section.c_str(), L"pivot_y", 0.5f), 0.0f, 1.0f);
            layer.blinkInterval = std::max(0.6f, ReadFloat(entry_, section.c_str(), L"blink_interval", 4.8f));
            layer.blinkDuration = std::clamp(ReadFloat(entry_, section.c_str(), L"blink_duration", 0.16f), 0.04f, 0.5f);

            const auto relative = fs::path(ReadText(entry_, section.c_str(), L"file", L""));
            if (relative.empty() || relative.is_absolute()) return false;
            layer.source = packageRoot_ / relative;
            if (!InsidePackage(layer.source, packageRoot_)) return false;
            if (!LoadBitmap(wic.Get(), layer.source, layer.bitmap.ReleaseAndGetAddressOf())) return false;
            layers_.push_back(std::move(layer));
        }
        return !layers_.empty();
    }

    void Draw(const ScenePaintContext& context, const D2D1_SIZE_F& size) {
        if (!target_ || !context.target || context.target != target_ || size.width <= 0.0f || size.height <= 0.0f) return;
        const float scale = std::max(size.width / designWidth_, size.height / designHeight_);
        const float offsetX = (size.width - designWidth_ * scale) * 0.5f;
        const float offsetY = (size.height - designHeight_ * scale) * 0.5f;

        D2D1_MATRIX_3X2_F baseTransform{};
        target_->GetTransform(&baseTransform);
        for (const auto& layer : layers_) DrawLayer(layer, context.time, scale, offsetX, offsetY, baseTransform);
        target_->SetTransform(baseTransform);
        DrawParticles(context, size);
        target_->SetTransform(baseTransform);
    }

private:
    bool LoadBitmap(IWICImagingFactory* wic, const fs::path& path, ID2D1Bitmap** output) {
        if (!wic || !target_ || !output) return false;
        *output = nullptr;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return false;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return false;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(wic->CreateFormatConverter(converter.GetAddressOf()))) return false;
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                         nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) return false;
        return SUCCEEDED(target_->CreateBitmapFromWicBitmap(converter.Get(), nullptr, output));
    }

    void DrawLayer(const Layer& layer, float time, float scale, float offsetX, float offsetY,
                   const D2D1_MATRIX_3X2_F& baseTransform) {
        if (!layer.bitmap) return;
        float x = layer.x;
        float y = layer.y;
        float layerScale = 1.0f;
        float angle = 0.0f;
        float opacity = layer.opacity;
        const float wave = std::sin(time * layer.speed + layer.phase);

        if (layer.animation == L"drift" || layer.animation == L"float") {
            x += wave * layer.amplitudeX;
            y += std::cos(time * layer.speed * 0.77f + layer.phase) * layer.amplitudeY;
        } else if (layer.animation == L"breathe") {
            layerScale += wave * layer.scaleAmplitude;
            y += std::cos(time * layer.speed + layer.phase) * layer.amplitudeY;
        } else if (layer.animation == L"sway") {
            x += wave * layer.amplitudeX;
            y += std::cos(time * layer.speed * 0.81f + layer.phase) * layer.amplitudeY;
            angle = wave * layer.rotationAmplitude;
        } else if (layer.animation == L"blink") {
            const float cycle = std::fmod(std::max(0.0f, time + layer.phase), layer.blinkInterval);
            if (cycle > layer.blinkDuration) opacity = 0.0f;
        }

        const float rawWidth = layer.width * scale;
        const float rawHeight = layer.height * scale;
        const float width = rawWidth * layerScale;
        const float height = rawHeight * layerScale;
        const float left = offsetX + x * scale - (width - rawWidth) * layer.pivotX;
        const float top = offsetY + y * scale - (height - rawHeight) * layer.pivotY;
        const D2D1_RECT_F destination = D2D1::RectF(left, top, left + width, top + height);

        if (std::fabs(angle) > 0.001f) {
            const D2D1_POINT_2F pivot = D2D1::Point2F(left + width * layer.pivotX, top + height * layer.pivotY);
            target_->SetTransform(D2D1::Matrix3x2F::Rotation(angle, pivot) * baseTransform);
        } else {
            target_->SetTransform(baseTransform);
        }
        target_->DrawBitmap(layer.bitmap.Get(), destination, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
        target_->SetTransform(baseTransform);
    }

    void DrawParticles(const ScenePaintContext& context, const D2D1_SIZE_F& size) {
        if (!context.brush || !context.target) return;
        for (int i = 0; i < sparkleCount_; ++i) {
            const float x = LocalHash01(static_cast<std::uint32_t>(i * 79 + 19)) * size.width;
            const float y = LocalHash01(static_cast<std::uint32_t>(i * 101 + 31)) * size.height * 0.78f;
            const float pulse = 0.22f + (0.5f + 0.5f * std::sin(context.time * (0.7f + (i % 5) * 0.13f) + i)) * 0.78f;
            const float radius = 0.8f + static_cast<float>(i % 4) * 0.42f;
            context.brush->SetColor(D2D1::ColorF(1.0f, 0.92f, 0.78f, particleOpacity_ * pulse));
            context.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), context.brush);
        }
        for (int i = 0; i < petalCount_; ++i) {
            const float speed = 0.010f + static_cast<float>(i % 5) * 0.0025f;
            const float p = LocalWrap01(LocalHash01(static_cast<std::uint32_t>(i * 43 + 7)) + context.time * speed);
            const float baseX = LocalHash01(static_cast<std::uint32_t>(i * 61 + 23)) * size.width;
            const float x = baseX + std::sin(context.time * 0.34f + i * 1.17f) * size.width * 0.018f;
            const float y = -size.height * 0.05f + p * size.height * 1.10f;
            const float rx = size.width * (0.0018f + static_cast<float>(i % 3) * 0.0006f);
            const float ry = size.height * (0.0042f + static_cast<float>(i % 4) * 0.0008f);
            context.brush->SetColor(D2D1::ColorF(1.0f, 0.66f, 0.83f,
                                                  particleOpacity_ * (0.35f + (1.0f - p) * 0.45f)));
            context.target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), rx, ry), context.brush);
        }
    }

    ID2D1RenderTarget* target_{};
    fs::path packageRoot_;
    fs::path entry_;
    float designWidth_{1672.0f};
    float designHeight_{941.0f};
    int sparkleCount_{22};
    int petalCount_{12};
    float particleOpacity_{0.46f};
    std::vector<Layer> layers_;
};

struct CacheKey {
    ID2D1RenderTarget* target{};
    std::wstring package;
    bool operator==(const CacheKey& other) const noexcept { return target == other.target && package == other.package; }
};

struct CacheKeyHash {
    std::size_t operator()(const CacheKey& key) const noexcept {
        const auto p = reinterpret_cast<std::uintptr_t>(key.target);
        return std::hash<std::uintptr_t>{}(p) ^ (std::hash<std::wstring>{}(key.package) << 1);
    }
};

struct CacheEntry {
    std::unique_ptr<Renderer> renderer;
    bool attempted{};
};

inline thread_local std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> gRenderers;

} // namespace layered_scene_detail

inline bool PaintPackagedScene(std::wstring_view packageName,
                               const ScenePaintContext& context,
                               const D2D1_SIZE_F& targetSize) {
    using namespace layered_scene_detail;
    if (!context.target || packageName.empty()) return false;
    const auto packageRoot = LocatePackage(packageName);
    if (packageRoot.empty()) return false;

    CacheKey key{context.target, std::wstring(packageName)};
    auto& entry = gRenderers[key];
    if (!entry.attempted) {
        entry.attempted = true;
        auto renderer = std::make_unique<Renderer>(context.target, packageRoot);
        if (renderer->Initialize()) entry.renderer = std::move(renderer);
    }
    if (!entry.renderer) return false;
    entry.renderer->Draw(context, targetSize);
    return true;
}

} // namespace miaodesk::wallpaper::scenes
