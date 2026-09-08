#pragma once

#include <string>
#include <string_view>

#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

// M4 bridge for particleEmitters while the generic scene serializer is being
// migrated toward a shared JSON DOM. The codec reads only the declarative
// particleEmitters field from an already-valid scene.json and appends it to the
// canonical SceneRuntimeDefinition; runtime validation remains authoritative.
class MiaoParticleSerializer {
public:
    static bool DeserializeEmitters(
        std::string_view sceneJsonUtf8,
        SceneRuntimeDefinition* runtime,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

} // namespace miaodesk::content
