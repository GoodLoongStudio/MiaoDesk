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
// `MiaoD3D11ParticleRenderer::SelfTest`, `MiaoD3D11TextureLoader::SelfTestPathPolicy` and
// the file-local `TransformMathSelfTest`.
//
// `MiaoD3D11RenderTargetPool::SelfTest` *used* to be in that list, on the stated grounds
// that it "needs d3d11.h". That was wrong twice over, and the correction is instructive:
// its *implementation* did sit in a .cpp that includes d3d11.h, but it is pure
// arithmetic, and moving it to MiaoD3D11RenderPolicy.cpp made it runnable here. The
// lesson is that "this needs d3d11.h" was doing duty as an explanation when it was only
// a description of where the code happened to be filed.
//
// The remaining three really are Windows-only, and for different reasons worth keeping
// apart: the particle renderer's *header* includes d3d11.h, so nothing about it can be
// reached here; `TransformMathSelfTest` is file-local to a Windows-only .cpp; and
// `SelfTestPathPolicy` looks platform-free but is not — one of its three assertions is
// `!IsSafeRelativePath(L"C:\\outside.png")`, and a drive letter plus a backslash only
// escapes a package where a backslash is a separator. Running it here returns false.
#include "miaodesk/MiaoGpuParameterBlock.h"
#include "miaodesk/MiaoParticleRuntime.h"
#include "miaodesk/MiaoPostProcessCompiler.h"
#include "miaodesk/MiaoPostProcessShaderLibrary.h"
#include "miaodesk/MiaoRenderGraph.h"
#include "miaodesk/MiaoD3D11RenderTarget.h"
#include "miaodesk/MiaoD3D11TextureLoader.h"
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
    // 第八项,来自 P3-6 第 2 步之后的一次更正:渲染目标池的尺寸自测此前也只在 Windows
    // 上跑,而它是纯算术 —— 只是实现躺在一个 include 了 d3d11.h 的 .cpp 里。
    // 现已搬进 MiaoD3D11RenderPolicy.cpp(平台无关),于是回到每台机器都能跑。
    Run("MiaoD3D11RenderTargetPool::SelfTest", MiaoD3D11RenderTargetPool::SelfTest);
    // 第九项:包内路径安全规则。它此前整个自测都被当成 Windows-only,而其中两条断言
    // (包内资源放行、../ 逃逸拒绝)在任何平台上语义相同。按平台拆开之后,
    // 这两条从"等一轮 Windows CI"变成"每次提交都在本机跑"。见 MiaoD3D11RenderPolicy.cpp。
    Run("MiaoD3D11TextureLoader::SelfTestPathPolicy", MiaoD3D11TextureLoader::SelfTestPathPolicy);

    std::printf("\n");
    if (failures != 0) {
        std::printf("FAILED:%d 项自测失败\n", failures);
        return 1;
    }
    std::printf("九项内容层自测全部通过 —— 它们此前在任何机器上都没有被执行过。\n");
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
