#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

struct MiaoSceneRuntimeState {
    bool loaded{};
    bool paintReady{};
    std::uint64_t generation{};
    std::wstring error;
};

class MiaoSceneRuntime {
public:
    bool Initialize(SceneRuntimeDefinition definition, std::wstring* error = nullptr);

    bool SetParameter(std::wstring_view id, PropertyValue value, std::wstring* error = nullptr);
    bool SetInput(std::wstring_view id, PropertyValue value, std::wstring* error = nullptr);

    const PropertyValue* GetParameter(std::wstring_view id) const noexcept;
    const PropertyValue* GetInput(std::wstring_view id) const noexcept;
    const PropertyValue* GetProperty(const PropertyAddress& address) const noexcept;

    std::vector<PropertyAddress> ConsumeDirtyProperties();
    const SceneRuntimeDefinition* Definition() const noexcept;
    const MiaoSceneRuntimeState& State() const noexcept;

    void MarkPaintReady(bool value = true) noexcept;
    void Reset() noexcept;

    static bool SelfTest();

private:
    bool ApplyBinding(const PropertyBindingDefinition& binding, const PropertyValue& source, std::wstring* error);
    bool ApplyBindingsForSource(BindingSourceKind kind, std::wstring_view sourceId, const PropertyValue& value, std::wstring* error);
    void MarkDirty(const PropertyAddress& address);

    SceneRuntimeDefinition definition_;
    bool hasDefinition_{};
    MiaoPropertyStore properties_;
    std::unordered_map<std::wstring, PropertyValue> parameterValues_;
    std::unordered_map<std::wstring, PropertyValue> inputValues_;
    std::vector<PropertyAddress> dirtyProperties_;
    MiaoSceneRuntimeState state_;
};

} // namespace miaodesk::content
