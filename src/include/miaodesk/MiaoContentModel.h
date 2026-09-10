#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/MiaoSceneModel.h"

namespace miaodesk::content {

enum class ContentRuntimeKind {
    Scene,
    Web,
};

enum class ContentParameterType {
    Bool,
    Int,
    Float,
    String,
    Color,
    Enum,
    Asset,
};

// Content parameters deliberately reuse the Scene property value ABI so Settings,
// AI patches and Scene Runtime do not need a second color/asset representation.
using ContentParameterValue = PropertyValue;
using ContentParameterValues = std::map<std::wstring, ContentParameterValue, std::less<>>;

struct ContentParameterDefinition {
    // runtimeId is the stable package/runtime reference (for example param://opacity).
    // key is the user/AI-facing stable key (for example opacity).
    std::wstring runtimeId;
    std::wstring key;
    ContentParameterType type{ContentParameterType::String};
    ContentParameterValue defaultValue{std::wstring{}};
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    std::vector<std::wstring> choices;
};

using ContentParameterSchema = std::vector<ContentParameterDefinition>;

struct ContentGeometryPolicy {
    float defaultWidth{1.0f};
    float defaultHeight{1.0f};
    bool resizeAllowed{false};
    float minWidth{0.05f};
    float minHeight{0.05f};
    float maxWidth{1.0f};
    float maxHeight{1.0f};
    std::optional<float> aspectRatio;
};

struct ContentDefinition {
    std::wstring id;
    std::wstring name;
    std::wstring version;
    ContentKind kind{ContentKind::Wallpaper};
    ContentRuntimeKind runtime{ContentRuntimeKind::Scene};
    std::filesystem::path entry;
    ContentGeometryPolicy geometry;
    ContentParameterSchema parameters;
    std::vector<std::wstring> capabilities;
};

struct ContentInstance {
    std::wstring instanceId;
    std::wstring definitionId;
    std::wstring monitorId;
    bool enabled{true};
    float x{};
    float y{};
    float width{1.0f};
    float height{1.0f};
    std::wstring fitMode{L"fill"};
    ContentParameterValues parameterValues;
};

class MiaoContentModel {
public:
    static bool ValidateDefinition(const ContentDefinition& definition, std::wstring* error = nullptr);

    static bool ResolveParameterValues(
        const ContentDefinition& definition,
        const ContentParameterValues& overrides,
        ContentParameterValues* resolved,
        std::wstring* error = nullptr);

    static bool ValidateInstance(
        const ContentDefinition& definition,
        const ContentInstance& instance,
        std::wstring* error = nullptr);

    static const ContentParameterDefinition* FindParameter(
        const ContentDefinition& definition,
        std::wstring_view key) noexcept;

    static const ContentParameterDefinition* FindParameterByRuntimeId(
        const ContentDefinition& definition,
        std::wstring_view runtimeId) noexcept;

    static bool SelfTest();
};

} // namespace miaodesk::content
