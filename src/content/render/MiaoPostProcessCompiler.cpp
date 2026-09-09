#include "miaodesk/MiaoPostProcessCompiler.h"

#include <string>
#include <utility>

namespace miaodesk::content {
namespace {

constexpr std::wstring_view kSceneColor = L"renderres://scene-color";
constexpr std::wstring_view kParticleColor = L"renderres://particle-color";
constexpr std::wstring_view kBackbuffer = L"renderres://backbuffer";
constexpr std::wstring_view kParticlePass = L"renderpass://particles";

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

std::wstring PostResourceId(std::size_t index) {
    return L"renderres://post/" + std::to_wstring(index);
}

std::wstring PostPassId(std::size_t index) {
    return L"renderpass://post/" + std::to_wstring(index);
}

} // namespace

const PostProcessPassPlan* MiaoPostProcessCompiler::FindPass(
    const PostProcessRenderPlan& plan,
    std::wstring_view renderPassId) noexcept {
    for (const auto& pass : plan.postProcessPasses) {
        if (pass.renderPassId == renderPassId) return &pass;
    }
    return nullptr;
}

bool MiaoPostProcessCompiler::Build(
    const SceneRuntimeDefinition& runtime,
    bool programmableScene,
    PostProcessRenderPlan* plan,
    std::wstring* error) {
    if (!plan) return Fail(error, L"Post-process render plan output is null.");

    std::wstring runtimeError;
    if (!MiaoSceneRuntimeModel::Validate(runtime, &runtimeError))
        return Fail(error, L"Cannot compile post-process plan from invalid runtime: " + runtimeError);

    PostProcessRenderPlan next;
    next.sceneColorResourceId = std::wstring(kSceneColor);
    next.backbufferResourceId = std::wstring(kBackbuffer);

    RenderResourceDefinition sceneColor;
    sceneColor.id = next.sceneColorResourceId;
    sceneColor.renderTarget = true;
    sceneColor.shaderResource = true;
    next.graph.resources.push_back(std::move(sceneColor));

    RenderResourceDefinition backbuffer;
    backbuffer.id = next.backbufferResourceId;
    backbuffer.external = true;
    backbuffer.renderTarget = true;
    backbuffer.shaderResource = false;
    next.graph.resources.push_back(std::move(backbuffer));

    next.graph.passes.push_back(RenderPassDefinition{
        programmableScene ? L"renderpass://programmable-scene" : L"renderpass://scene",
        programmableScene ? RenderPassKind::Programmable : RenderPassKind::Scene2D,
        {},
        {next.sceneColorResourceId},
        true,
    });

    // A particle pass cannot write SceneColor directly because RenderGraph v1
    // intentionally enforces a single writer per resource. Instead it reads the
    // completed scene and writes a new color target. The D3D11 particle backend
    // copies SceneColor first and then alpha-composites instances on top.
    std::wstring previousResource = next.sceneColorResourceId;
    if (!runtime.particleEmitters.empty()) {
        RenderResourceDefinition particleColor;
        particleColor.id = std::wstring(kParticleColor);
        particleColor.renderTarget = true;
        particleColor.shaderResource = true;
        next.graph.resources.push_back(std::move(particleColor));

        next.graph.passes.push_back(RenderPassDefinition{
            std::wstring(kParticlePass),
            RenderPassKind::Particle,
            {next.sceneColorResourceId},
            {std::wstring(kParticleColor)},
            true,
        });
        previousResource = std::wstring(kParticleColor);
    }

    std::size_t enabledIndex = 0;
    bool bloomBranchActive = false;
    std::wstring bloomSourceResource;
    for (const auto& effect : runtime.postProcesses) {
        if (!effect.enabled) continue;

        if (effect.effect == PostProcessEffectKind::BloomThreshold) {
            bloomBranchActive = true;
            bloomSourceResource = previousResource;
        } else if (effect.effect == PostProcessEffectKind::BloomCombine) {
            if (!bloomBranchActive || bloomSourceResource.empty())
                return Fail(error, L"BloomCombine requires an active BloomThreshold branch before it: " + effect.id);
        } else if (bloomBranchActive &&
                   effect.effect != PostProcessEffectKind::BlurHorizontal &&
                   effect.effect != PostProcessEffectKind::BlurVertical) {
            // Keep the v1 bloom branch deterministic: Threshold → zero or more
            // blur passes → Combine. More general branch graphs can be exposed
            // later without making this package-facing list ambiguous.
            bloomBranchActive = false;
            bloomSourceResource.clear();
        }

        const auto outputId = PostResourceId(enabledIndex);
        const auto passId = PostPassId(enabledIndex);
        const bool combinesWithScene = effect.effect == PostProcessEffectKind::BloomCombine;
        const std::wstring auxiliaryInput = combinesWithScene ? bloomSourceResource : std::wstring{};

        RenderResourceDefinition output;
        output.id = outputId;
        output.renderTarget = true;
        output.shaderResource = true;
        next.graph.resources.push_back(std::move(output));

        std::vector<std::wstring> reads{previousResource};
        if (!auxiliaryInput.empty() && auxiliaryInput != previousResource) reads.push_back(auxiliaryInput);
        next.graph.passes.push_back(RenderPassDefinition{
            passId,
            RenderPassKind::PostProcess,
            std::move(reads),
            {outputId},
            true,
        });

        next.postProcessPasses.push_back(PostProcessPassPlan{
            passId,
            previousResource,
            auxiliaryInput,
            outputId,
            effect,
        });

        previousResource = outputId;
        if (combinesWithScene) {
            bloomBranchActive = false;
            bloomSourceResource.clear();
        }
        ++enabledIndex;
    }

    next.finalColorResourceId = previousResource;
    next.graph.passes.push_back(RenderPassDefinition{
        L"renderpass://composite",
        RenderPassKind::Composite,
        {next.finalColorResourceId},
        {next.backbufferResourceId},
        true,
    });
    next.graph.passes.push_back(RenderPassDefinition{
        L"renderpass://present",
        RenderPassKind::Present,
        {next.backbufferResourceId},
        {},
        true,
    });

    std::wstring graphError;
    if (!MiaoRenderGraph::Compile(next.graph, &next.compiledGraph, &graphError))
        return Fail(error, L"Cannot compile post-process render graph: " + graphError);

    *plan = std::move(next);
    if (error) error->clear();
    return true;
}

bool MiaoPostProcessCompiler::SelfTest() {
    SceneRuntimeDefinition runtime;
    runtime.scene.id = L"scene://post-compiler-self-test";
    runtime.scene.kind = ContentKind::Wallpaper;
    runtime.scene.rootNodeId = L"node://root";
    runtime.profile = RuntimeProfile::Wallpaper;

    SceneNodeDefinition root;
    root.id = L"node://root";
    root.name = L"Root";
    root.components.push_back(SceneComponentDefinition{
        L"component://root/transform",
        ComponentKind::Transform,
        {},
    });
    runtime.scene.nodes.push_back(std::move(root));

    runtime.postProcesses.push_back(PostProcessDefinition{
        L"postfx://vignette",
        PostProcessEffectKind::Vignette,
        true,
        0.8,
        0.75,
        0.25,
    });
    runtime.postProcesses.push_back(PostProcessDefinition{
        L"postfx://disabled-noise",
        PostProcessEffectKind::Noise,
        false,
        0.2,
        0.5,
        0.1,
    });
    runtime.postProcesses.push_back(PostProcessDefinition{
        L"postfx://color",
        PostProcessEffectKind::ColorMatrix,
        true,
        1.0,
        0.75,
        0.25,
    });

    PostProcessRenderPlan plan;
    std::wstring error;
    if (!Build(runtime, true, &plan, &error)) return false;
    if (plan.postProcessPasses.size() != 2) return false;
    if (plan.graph.resources.size() != 4) return false;
    if (plan.graph.passes.size() != 5) return false;
    if (plan.finalColorResourceId != L"renderres://post/1") return false;
    if (plan.compiledGraph.passOrder.size() != plan.graph.passes.size()) return false;

    const auto* first = FindPass(plan, L"renderpass://post/0");
    if (!first || first->effect.id != L"postfx://vignette") return false;
    if (first->inputResourceId != L"renderres://scene-color") return false;
    if (!first->auxiliaryInputResourceId.empty()) return false;
    if (first->outputResourceId != L"renderres://post/0") return false;

    SceneRuntimeDefinition bloom = runtime;
    bloom.postProcesses.clear();
    bloom.postProcesses.push_back(PostProcessDefinition{
        L"postfx://bloom-threshold", PostProcessEffectKind::BloomThreshold, true, 1.0, 0.65, 0.1,
    });
    bloom.postProcesses.push_back(PostProcessDefinition{
        L"postfx://bloom-h", PostProcessEffectKind::BlurHorizontal, true, 1.0, 0.7, 0.2,
    });
    bloom.postProcesses.push_back(PostProcessDefinition{
        L"postfx://bloom-v", PostProcessEffectKind::BlurVertical, true, 1.0, 0.7, 0.2,
    });
    bloom.postProcesses.push_back(PostProcessDefinition{
        L"postfx://bloom-combine", PostProcessEffectKind::BloomCombine, true, 0.8, 0.75, 0.25,
    });
    if (!Build(bloom, true, &plan, &error)) return false;
    if (plan.postProcessPasses.size() != 4) return false;
    const auto* combine = FindPass(plan, L"renderpass://post/3");
    if (!combine || combine->effect.effect != PostProcessEffectKind::BloomCombine) return false;
    if (combine->inputResourceId != L"renderres://post/2") return false;
    if (combine->auxiliaryInputResourceId != L"renderres://scene-color") return false;
    const auto& combineGraphPass = plan.graph.passes[4];
    if (combineGraphPass.reads.size() != 2 || combineGraphPass.reads[1] != L"renderres://scene-color") return false;

    SceneRuntimeDefinition invalidBloom = runtime;
    invalidBloom.postProcesses.clear();
    invalidBloom.postProcesses.push_back(PostProcessDefinition{
        L"postfx://orphan-combine", PostProcessEffectKind::BloomCombine, true, 1.0, 0.75, 0.25,
    });
    if (Build(invalidBloom, true, &plan, &error)) return false;

    SceneRuntimeDefinition withParticles = runtime;
    withParticles.postProcesses.clear();
    ParticleEmitterDefinition emitter;
    emitter.id = L"particle://post-compiler-self-test";
    emitter.maxParticles = 32;
    emitter.spawnRate = 16.0;
    withParticles.particleEmitters.push_back(emitter);
    if (!Build(withParticles, false, &plan, &error)) return false;
    if (plan.graph.resources.size() != 3) return false;
    if (plan.graph.passes.size() != 4) return false;
    if (plan.finalColorResourceId != L"renderres://particle-color") return false;
    if (plan.graph.passes[1].kind != RenderPassKind::Particle ||
        plan.graph.passes[1].reads.size() != 1 ||
        plan.graph.passes[1].reads[0] != L"renderres://scene-color" ||
        plan.graph.passes[1].writes.size() != 1 ||
        plan.graph.passes[1].writes[0] != L"renderres://particle-color") return false;

    SceneRuntimeDefinition particleBloom = withParticles;
    particleBloom.postProcesses = bloom.postProcesses;
    if (!Build(particleBloom, true, &plan, &error)) return false;
    if (plan.postProcessPasses.size() != 4) return false;
    const auto* particleCombine = FindPass(plan, L"renderpass://post/3");
    if (!particleCombine || particleCombine->auxiliaryInputResourceId != L"renderres://particle-color") return false;
    const auto* threshold = FindPass(plan, L"renderpass://post/0");
    if (!threshold || threshold->inputResourceId != L"renderres://particle-color") return false;

    SceneRuntimeDefinition noEffects = runtime;
    noEffects.postProcesses.clear();
    if (!Build(noEffects, false, &plan, &error)) return false;
    if (!plan.postProcessPasses.empty()) return false;
    if (plan.finalColorResourceId != L"renderres://scene-color") return false;
    if (plan.graph.passes.size() != 3) return false;

    return true;
}

} // namespace miaodesk::content
