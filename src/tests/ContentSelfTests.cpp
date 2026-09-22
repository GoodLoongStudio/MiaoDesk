// Runs the content-layer SelfTests that only ever hung off an aggregator with no caller.
//
// The situation this file fixes: `MiaoSceneD3D11Renderer::SelfTest()` is
// Windows-only and **nothing called it**, and inside it sat
// `MiaoRenderGraph::SelfTest() && MiaoPostProcessCompiler::SelfTest() && ...` — seven
// pure-logic self-tests, every one of them reachable only through that dead call, and
// none of them run anywhere else. Between them they are ~290 lines of assertions about
// the render graph, the post-process compiler, the shader library, the shader ABI
// contract, GPU parameter packing and the particle runtime, plus
// `MiaoSceneRuntimeModel::SelfTest()` (build + parameter + input resolution), which the
// aggregator did not even include and which has no other caller either.
//
// So the same defect recurred a third time this session, and each time the fix is the
// same: a self-test with no caller is indistinguishable from one that passes. These are
// pure logic — no D3D11 device, no Windows headers — so they run here on every machine
// rather than waiting for a 90-second Windows round trip to tell us they broke.
//
// Windows-only members of that aggregator are deliberately *not* called from here:
// `MiaoD3D11ParticleRenderer::SelfTest`, `MiaoD3D11TextureLoader::SelfTestPathPolicy`,
// `MiaoD3D11RenderTargetPool::SelfTest` and the file-local `TransformMathSelfTest` all
// need d3d11.h / d3dcompiler.h. They need a Windows target, which is a separate piece
// of work from this one.
#include "miaodesk/MiaoGpuParameterBlock.h"
#include "miaodesk/MiaoParticleRuntime.h"
#include "miaodesk/MiaoPostProcessCompiler.h"
#include "miaodesk/MiaoPostProcessShaderLibrary.h"
#include "miaodesk/MiaoRenderGraph.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoShaderContract.h"

#include <cstdio>

using namespace miaodesk::content;

namespace {

int failures = 0;

void Run(const char* what, bool (*fn)()) {
    std::printf("  [%s] %s\n", fn() ? "ok  " : "FAIL", what);
    if (!fn()) ++failures;
}

} // namespace

int wmain() {
    std::printf("\n1. 内容层 SelfTest(此前只经由一个无人调用的 D3D11 聚合器可达)\n");

    Run("MiaoRenderGraph::SelfTest", MiaoRenderGraph::SelfTest);
    Run("MiaoPostProcessCompiler::SelfTest", MiaoPostProcessCompiler::SelfTest);
    Run("MiaoPostProcessShaderLibrary::SelfTest", MiaoPostProcessShaderLibrary::SelfTest);
    Run("MiaoShaderContract::SelfTest", MiaoShaderContract::SelfTest);
    Run("MiaoGpuParameterPacker::SelfTest", MiaoGpuParameterPacker::SelfTest);
    Run("MiaoParticleRuntime::SelfTest", MiaoParticleRuntime::SelfTest);
    Run("MiaoSceneRuntimeModel::SelfTest", MiaoSceneRuntimeModel::SelfTest);

    std::printf("\n");
    if (failures != 0) {
        std::printf("FAILED:%d 项自测失败\n", failures);
        return 1;
    }
    std::printf("七项内容层自测全部通过 —— 它们此前在任何机器上都没有被执行过。\n");
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
