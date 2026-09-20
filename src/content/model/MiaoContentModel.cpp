#include "miaodesk/MiaoContentModel.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool ValidId(std::wstring_view value) {
    if (value.empty() || value.size() > 160) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
               (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-' || ch == L'_';
    });
}

bool ValidRuntimeId(std::wstring_view value) {
    if (value.empty() || value.size() > 256) return false;
    return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
               (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-' || ch == L'_' ||
               ch == L':' || ch == L'/';
    });
}

bool FinitePositive(float value) {
    return std::isfinite(value) && value > 0.0f;
}

bool SameFloat(float left, float right) {
    return std::fabs(left - right) <= 0.0001f;
}

bool FiniteColor(const Color4& color) {
    return std::isfinite(color.r) && std::isfinite(color.g) &&
           std::isfinite(color.b) && std::isfinite(color.a);
}

bool ValueTypeMatches(ContentParameterType type, const ContentParameterValue& value) {
    switch (type) {
    case ContentParameterType::Bool:
        return std::holds_alternative<bool>(value);
    case ContentParameterType::Int:
        return std::holds_alternative<std::int64_t>(value);
    case ContentParameterType::Float:
        return std::holds_alternative<double>(value);
    case ContentParameterType::String:
    case ContentParameterType::Enum:
        return std::holds_alternative<std::wstring>(value);
    case ContentParameterType::Color:
        return std::holds_alternative<Color4>(value);
    case ContentParameterType::Asset:
        return std::holds_alternative<AssetReference>(value);
    }
    return false;
}

bool NumericValue(const ContentParameterValue& value, double* number) {
    if (!number) return false;
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        *number = static_cast<double>(*integer);
        return true;
    }
    if (const auto* floating = std::get_if<double>(&value)) {
        *number = *floating;
        return true;
    }
    return false;
}

bool ValidateParameterValue(
    const ContentParameterDefinition& parameter,
    const ContentParameterValue& value,
    std::wstring* error) {
    if (!ValueTypeMatches(parameter.type, value))
        return Fail(error, L"Content parameter type mismatch: " + parameter.key);

    double number = 0.0;
    if (NumericValue(value, &number)) {
        if (!std::isfinite(number))
            return Fail(error, L"Content numeric parameter must be finite: " + parameter.key);
        if (parameter.minimum && number < *parameter.minimum)
            return Fail(error, L"Content parameter is below minimum: " + parameter.key);
        if (parameter.maximum && number > *parameter.maximum)
            return Fail(error, L"Content parameter is above maximum: " + parameter.key);
    }

    if (parameter.type == ContentParameterType::String) {
        const auto* text = std::get_if<std::wstring>(&value);
        if (!text || text->size() > 16384)
            return Fail(error, L"Content string parameter is too large: " + parameter.key);
    }

    if (parameter.type == ContentParameterType::Color) {
        const auto* color = std::get_if<Color4>(&value);
        if (!color || !FiniteColor(*color))
            return Fail(error, L"Content color parameter must contain finite channels: " + parameter.key);
    }

    if (parameter.type == ContentParameterType::Asset) {
        const auto* asset = std::get_if<AssetReference>(&value);
        if (!asset || asset->id.empty() || asset->id.size() > 512)
            return Fail(error, L"Content asset parameter must reference a stable asset id: " + parameter.key);
    }

    if (parameter.type == ContentParameterType::Enum) {
        const auto* selected = std::get_if<std::wstring>(&value);
        if (!selected || std::find(parameter.choices.begin(), parameter.choices.end(), *selected) == parameter.choices.end())
            return Fail(error, L"Content enum parameter has an unsupported value: " + parameter.key);
    }
    return true;
}

bool ValidateGeometry(const ContentDefinition& definition, std::wstring* error) {
    const auto& geometry = definition.geometry;
    if (!FinitePositive(geometry.defaultWidth) || !FinitePositive(geometry.defaultHeight) ||
        !FinitePositive(geometry.minWidth) || !FinitePositive(geometry.minHeight) ||
        !FinitePositive(geometry.maxWidth) || !FinitePositive(geometry.maxHeight))
        return Fail(error, L"Content geometry dimensions must be finite and positive.");

    if (geometry.minWidth > geometry.maxWidth || geometry.minHeight > geometry.maxHeight)
        return Fail(error, L"Content geometry minimum exceeds maximum.");
    if (geometry.defaultWidth < geometry.minWidth || geometry.defaultWidth > geometry.maxWidth ||
        geometry.defaultHeight < geometry.minHeight || geometry.defaultHeight > geometry.maxHeight)
        return Fail(error, L"Content default geometry is outside its allowed range.");

    if (definition.kind == ContentKind::Widget &&
        (geometry.defaultWidth > 1.0f || geometry.defaultHeight > 1.0f ||
         geometry.maxWidth > 1.0f || geometry.maxHeight > 1.0f))
        return Fail(error, L"Widget geometry must use normalized desktop dimensions in the range (0, 1].");

    if (geometry.aspectRatio && (!FinitePositive(*geometry.aspectRatio)))
        return Fail(error, L"Content geometry aspect ratio must be finite and positive.");
    return true;
}

} // namespace

const ContentParameterDefinition* MiaoContentModel::FindParameter(
    const ContentDefinition& definition,
    std::wstring_view key) noexcept {
    const auto found = std::find_if(definition.parameters.begin(), definition.parameters.end(),
        [&](const ContentParameterDefinition& parameter) { return parameter.key == key; });
    return found == definition.parameters.end() ? nullptr : &*found;
}

const ContentParameterDefinition* MiaoContentModel::FindParameterByRuntimeId(
    const ContentDefinition& definition,
    std::wstring_view runtimeId) noexcept {
    const auto found = std::find_if(definition.parameters.begin(), definition.parameters.end(),
        [&](const ContentParameterDefinition& parameter) { return parameter.runtimeId == runtimeId; });
    return found == definition.parameters.end() ? nullptr : &*found;
}

bool MiaoContentModel::ValidateDefinition(const ContentDefinition& definition, std::wstring* error) {
    if (error) error->clear();
    if (!ValidId(definition.id)) return Fail(error, L"Content definition id is invalid.");
    if (definition.name.empty()) return Fail(error, L"Content definition name is required.");
    if (definition.version.empty()) return Fail(error, L"Content definition version is required.");
    if (definition.entry.empty() || definition.entry.is_absolute())
        return Fail(error, L"Content definition entry must be a relative package path.");
    if (!ValidateGeometry(definition, error)) return false;

    std::set<std::wstring, std::less<>> parameterKeys;
    std::set<std::wstring, std::less<>> runtimeIds;
    for (const auto& parameter : definition.parameters) {
        if (!ValidId(parameter.key)) return Fail(error, L"Content parameter key is invalid: " + parameter.key);
        if (!parameterKeys.insert(parameter.key).second)
            return Fail(error, L"Duplicate content parameter key: " + parameter.key);
        if (!ValidRuntimeId(parameter.runtimeId))
            return Fail(error, L"Content parameter runtime id is invalid: " + parameter.runtimeId);
        if (!runtimeIds.insert(parameter.runtimeId).second)
            return Fail(error, L"Duplicate content parameter runtime id: " + parameter.runtimeId);
        if (parameter.minimum && parameter.maximum && *parameter.minimum > *parameter.maximum)
            return Fail(error, L"Content parameter minimum exceeds maximum: " + parameter.key);
        if ((parameter.minimum || parameter.maximum || parameter.step) &&
            parameter.type != ContentParameterType::Int && parameter.type != ContentParameterType::Float)
            return Fail(error, L"Only numeric content parameters may declare min/max/step: " + parameter.key);
        if (parameter.step) {
            if (!std::isfinite(*parameter.step) || *parameter.step <= 0.0)
                return Fail(error, L"Content parameter step must be finite and positive: " + parameter.key);
            if (parameter.type == ContentParameterType::Int && std::floor(*parameter.step) != *parameter.step)
                return Fail(error, L"Integer content parameter step must be an integer: " + parameter.key);
        }
        if (parameter.type == ContentParameterType::Enum) {
            if (parameter.choices.empty())
                return Fail(error, L"Enum content parameter requires choices: " + parameter.key);
            std::set<std::wstring, std::less<>> uniqueChoices;
            for (const auto& choice : parameter.choices) {
                if (choice.empty() || !uniqueChoices.insert(choice).second)
                    return Fail(error, L"Enum content parameter choices must be non-empty and unique: " + parameter.key);
            }
        } else if (!parameter.choices.empty()) {
            return Fail(error, L"Only enum content parameters may declare choices: " + parameter.key);
        }
        if (!ValidateParameterValue(parameter, parameter.defaultValue, error)) return false;
    }

    std::set<std::wstring, std::less<>> capabilities;
    for (const auto& capability : definition.capabilities) {
        if (!ValidId(capability)) return Fail(error, L"Content capability id is invalid: " + capability);
        if (!capabilities.insert(capability).second)
            return Fail(error, L"Duplicate content capability: " + capability);
    }
    return true;
}

bool MiaoContentModel::ResolveParameterValues(
    const ContentDefinition& definition,
    const ContentParameterValues& overrides,
    ContentParameterValues* resolved,
    std::wstring* error) {
    if (!resolved) return Fail(error, L"Content parameter output is required.");
    if (!ValidateDefinition(definition, error)) return false;

    ContentParameterValues next;
    for (const auto& parameter : definition.parameters)
        next.emplace(parameter.key, parameter.defaultValue);

    for (const auto& [key, value] : overrides) {
        const auto* parameter = FindParameter(definition, key);
        if (!parameter) return Fail(error, L"Unknown content parameter override: " + key);
        if (!ValidateParameterValue(*parameter, value, error)) return false;
        next[key] = value;
    }
    *resolved = std::move(next);
    return true;
}

bool MiaoContentModel::ValidateInstance(
    const ContentDefinition& definition,
    const ContentInstance& instance,
    std::wstring* error) {
    if (!ValidateDefinition(definition, error)) return false;
    if (!ValidId(instance.instanceId)) return Fail(error, L"Content instance id is invalid.");
    if (instance.definitionId != definition.id)
        return Fail(error, L"Content instance definition id does not match its definition.");

    ContentParameterValues resolved;
    if (!ResolveParameterValues(definition, instance.parameterValues, &resolved, error)) return false;

    if (definition.kind == ContentKind::Widget) {
        const auto& geometry = definition.geometry;
        if (!std::isfinite(instance.x) || !std::isfinite(instance.y) ||
            !FinitePositive(instance.width) || !FinitePositive(instance.height))
            return Fail(error, L"Widget instance geometry is invalid.");
        if (instance.x < 0.0f || instance.y < 0.0f || instance.x > 1.0f || instance.y > 1.0f)
            return Fail(error, L"Widget instance position must use normalized desktop coordinates.");

        if (!geometry.resizeAllowed) {
            if (!SameFloat(instance.width, geometry.defaultWidth) || !SameFloat(instance.height, geometry.defaultHeight))
                return Fail(error, L"This content definition owns a fixed widget size.");
        } else {
            if (instance.width < geometry.minWidth || instance.width > geometry.maxWidth ||
                instance.height < geometry.minHeight || instance.height > geometry.maxHeight)
                return Fail(error, L"Widget instance size is outside the definition geometry policy.");
        }
        if (instance.x + instance.width > 1.0001f || instance.y + instance.height > 1.0001f)
            return Fail(error, L"Widget instance extends beyond its normalized monitor bounds.");
        if (geometry.aspectRatio && !SameFloat(instance.width / instance.height, *geometry.aspectRatio))
            return Fail(error, L"Widget instance violates the definition aspect ratio policy.");
    } else if (instance.fitMode.empty()) {
        return Fail(error, L"Wallpaper content instance requires a fit mode.");
    }
    return true;
}

bool MiaoContentModel::SelfTest() {
    ContentDefinition clock;
    clock.id = L"builtin.glass-clock";
    clock.name = L"Glass Clock";
    clock.version = L"1.0.0";
    clock.kind = ContentKind::Widget;
    clock.runtime = ContentRuntimeKind::Scene;
    clock.entry = L"scene.json";
    clock.geometry.defaultWidth = 0.30f;
    clock.geometry.defaultHeight = 0.30f;
    clock.geometry.resizeAllowed = false;
    clock.geometry.minWidth = 0.30f;
    clock.geometry.minHeight = 0.30f;
    clock.geometry.maxWidth = 0.30f;
    clock.geometry.maxHeight = 0.30f;
    clock.capabilities = {L"clock.read"};

    ContentParameterDefinition opacity;
    opacity.runtimeId = L"param://background-opacity";
    opacity.key = L"backgroundOpacity";
    opacity.type = ContentParameterType::Float;
    opacity.defaultValue = 0.55;
    opacity.minimum = 0.0;
    opacity.maximum = 1.0;
    opacity.step = 0.01;
    clock.parameters.push_back(opacity);

    ContentParameterDefinition format;
    format.runtimeId = L"param://time-format";
    format.key = L"timeFormat";
    format.type = ContentParameterType::Enum;
    format.defaultValue = std::wstring(L"24h");
    format.choices = {L"24h", L"12h"};
    clock.parameters.push_back(format);

    ContentParameterDefinition accent;
    accent.runtimeId = L"param://accent-color";
    accent.key = L"accentColor";
    accent.type = ContentParameterType::Color;
    accent.defaultValue = Color4{0.45, 0.65, 1.0, 1.0};
    clock.parameters.push_back(accent);

    std::wstring error;
    if (!ValidateDefinition(clock, &error)) return false;
    if (!FindParameterByRuntimeId(clock, L"param://background-opacity")) return false;

    ContentParameterValues overrides;
    overrides.emplace(L"backgroundOpacity", 0.40);
    overrides.emplace(L"timeFormat", std::wstring(L"12h"));
    ContentParameterValues resolved;
    if (!ResolveParameterValues(clock, overrides, &resolved, &error)) return false;
    if (resolved.size() != 3 || std::get<double>(resolved.at(L"backgroundOpacity")) != 0.40 ||
        std::get<std::wstring>(resolved.at(L"timeFormat")) != L"12h" ||
        !std::holds_alternative<Color4>(resolved.at(L"accentColor"))) return false;

    ContentParameterValues invalidOverride;
    invalidOverride.emplace(L"backgroundOpacity", 1.5);
    if (ResolveParameterValues(clock, invalidOverride, &resolved, &error)) return false;

    ContentInstance instance;
    instance.instanceId = L"clock-1";
    instance.definitionId = clock.id;
    instance.x = 0.65f;
    instance.y = 0.05f;
    instance.width = 0.30f;
    instance.height = 0.30f;
    instance.parameterValues = overrides;
    if (!ValidateInstance(clock, instance, &error)) return false;

    instance.width = 0.35f;
    if (ValidateInstance(clock, instance, &error)) return false;

    ContentDefinition bad = clock;
    bad.parameters[0].step = -0.01;
    if (ValidateDefinition(bad, &error)) return false;
    return true;
}

} // namespace miaodesk::content
