#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/MiaoRenderGraph.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

struct PostProcessPassPlan {
    std::wstring renderPassId;
    std::wstring inputResourceId;
    std::wstring outputResourceId;
    PostProcessDefinition effect;
};

struct PostProcessRenderPlan {
    RenderGraphDefinition graph;
    CompiledRenderGraph compiledGraph;
    std::vector<PostProcessPassPlan> postProcessPasses;
    std::wstring sceneColorResourceId;
    std::wstring finalColorResourceId;
    std::wstring backbufferResourceId;
};

// Converts the declarative SceneRuntimeDefinition::postProcesses list into the
// concrete render resources/passes used by the GPU backend. Content never owns
// these generated render-resource ids; they are renderer implementation detail.
class MiaoPostProcessCompiler {
public:
    static bool Build(
        const SceneRuntimeDefinition& runtime,
        bool programmableScene,
        PostProcessRenderPlan* plan,
        std::wstring* error = nullptr);

    static const PostProcessPassPlan* FindPass(
        const PostProcessRenderPlan& plan,
        std::wstring_view renderPassId) noexcept;

    static bool SelfTest();
};

} // namespace miaodesk::content
