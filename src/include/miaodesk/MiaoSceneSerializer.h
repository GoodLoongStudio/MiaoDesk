#pragma once

#include <string>
#include <string_view>

#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

class MiaoSceneSerializer {
public:
    static bool Deserialize(
        std::string_view sceneJsonUtf8,
        std::string_view parameterJsonUtf8,
        SceneRuntimeDefinition* runtime,
        std::wstring* error = nullptr);

    static bool DeserializePackage(
        const LoadedMiaoContentPackage& package,
        SceneRuntimeDefinition* runtime,
        std::wstring* error = nullptr);

    static bool SerializeScene(
        const SceneRuntimeDefinition& runtime,
        std::string* sceneJsonUtf8,
        std::wstring* error = nullptr);

    static bool SerializeParameters(
        const SceneRuntimeDefinition& runtime,
        std::string* parameterJsonUtf8,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

} // namespace miaodesk::content
