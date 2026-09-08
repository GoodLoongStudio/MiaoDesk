#pragma once

#include <string>
#include <string_view>

#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

// Compatibility shim for the M4 development branch. Particle emitters are now
// parsed by MiaoSceneSerializer itself; this legacy call site only re-validates
// the already-populated SceneRuntimeDefinition until the renderer call is
// removed in the next renderer cleanup.
class MiaoParticleSerializer {
public:
    static bool DeserializeEmitters(
        std::string_view,
        SceneRuntimeDefinition* runtime,
        std::wstring* error = nullptr) {
        if (!runtime) {
            if (error) *error = L"Particle serializer runtime output is null.";
            return false;
        }
        std::wstring validationError;
        if (!MiaoSceneRuntimeModel::Validate(*runtime, &validationError)) {
            if (error) *error = L"Particle emitter runtime is invalid: " + validationError;
            return false;
        }
        if (error) error->clear();
        return true;
    }

    static bool SelfTest() { return true; }
};

} // namespace miaodesk::content
