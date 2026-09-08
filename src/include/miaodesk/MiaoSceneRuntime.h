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
    bool AdvanceTimeline(double timeSeconds, std::wstring* error = nullptr);

    const PropertyValue* GetParameter(std::wstring_view id) const noexcept;
    const PropertyValue* GetInput(std::wstring_view id) const noexcept;
    const PropertyValue* GetProperty(const PropertyAddress& address) const noexcept;

    // Used by future Widget/Wallpaper schedulers to distinguish content that
    // genuinely needs another frame from content that can sleep at 0 FPS.
    bool NeedsContinuousAnimation(double timeSeconds) const noexcept;

    std::vector<PropertyAddress> ConsumeDirtyProperties();
    const SceneRuntimeDefinition* Definition() const noexcept;
    const MiaoSceneRuntimeState& State() const noexcept;

    void MarkPaintReady(bool value = true) noexcept;
    void Reset() noexcept;

    static bool SelfTest();

private:
    struct AnimationPlaybackState {
        bool triggered{};
        double startTime{};
    };

    bool ApplyBinding(const PropertyBindingDefinition& binding, const PropertyValue& source, std::wstring* error);
    bool ApplyBindingsForSource(BindingSourceKind kind, std::wstring_view sourceId, const PropertyValue& value, std::wstring* error);
    bool ApplyAnimation(const AnimationTrackDefinition& animation, double timeSeconds, std::wstring* error);
    bool TriggerAnimationsForInput(
        std::wstring_view inputId,
        const PropertyValue* previous,
        const PropertyValue& current,
        std::wstring* error);
    void MarkDirty(const PropertyAddress& address);

    SceneRuntimeDefinition definition_;
    bool hasDefinition_{};
    MiaoPropertyStore properties_;
    std::unordered_map<std::wstring, PropertyValue> parameterValues_;
    std::unordered_map<std::wstring, PropertyValue> inputValues_;
    std::unordered_map<std::wstring, AnimationPlaybackState> animationPlayback_;
    std::vector<PropertyAddress> dirtyProperties_;
    double timelineTime_{};
    MiaoSceneRuntimeState state_;
};

} // namespace miaodesk::content
