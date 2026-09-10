#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
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

using ContentParameterValue = std::variant<bool, std::int64_t, double, std::wstring>;
using ContentParameterValues = std::map<std::wstring, ContentParameterValue, std::less<>>;

struct ContentParameterDefinition {
    std::wstring key;
    ContentParameterType type{ContentParameterType::String};
    ContentParameterValue defaultValue{std::wstring{}};
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::vector<std::wstring> choices;
};

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
    std::vector<ContentParameterDefinition> parameters;
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

    static bool SelfTest();
};

} // namespace miaodesk::content
