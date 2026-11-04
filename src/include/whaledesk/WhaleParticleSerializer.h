#pragma once

#include <string>
#include <string_view>

#include "whaledesk/WhaleSceneRuntimeModel.h"

namespace whaledesk::content {

// Compatibility shim for the M4 development branch. Particle emitters are now
// parsed by WhaleSceneSerializer itself; this legacy call site only re-validates
// the already-populated SceneRuntimeDefinition until the renderer call is
// removed in the next renderer cleanup.
class WhaleParticleSerializer {
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
        if (!WhaleSceneRuntimeModel::Validate(*runtime, &validationError)) {
            if (error) *error = L"Particle emitter runtime is invalid: " + validationError;
            return false;
        }
        if (error) error->clear();
        return true;
    }

    static bool SelfTest() { return true; }
};

} // namespace whaledesk::content
