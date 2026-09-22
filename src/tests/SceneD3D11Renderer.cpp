// Runs what is left of the D3D11 renderer self-tests after the platform-free parts moved.
//
// This is P3-6 step 2, and the order it was written in matters. Step 1 moved the four
// D3D11 renderer .cpp into a MiaoDeskSceneD3D11 library; that change had to be proved
// to compile and link under MSVC *before* a test target was allowed to depend on it,
// because otherwise a link failure could not be attributed to either change. That proof
// arrived as green `build` runs, so this target links the library instead of compiling
// a second copy of its sources.
//
// Two of the four self-tests used to be called here and no longer are, because the
// reason they were Windows-only turned out to be "the file they were filed in includes
// d3d11.h" rather than anything about the code:
//
//   · MiaoD3D11RenderTargetPool::SelfTest       pure arithmetic
//   · MiaoD3D11TextureLoader::SelfTestPathPolicy  split by platform; two of its three
//     assertions hold everywhere, and the third is the platform's own escape case
//
// Both are in ContentSelfTests now, which runs on every machine. Removing them from here
// was not tidiness for its own sake: a check that can run everywhere should, and a target
// that claims to be Windows-only should not be carrying portable work — otherwise the
// next person reads the skip-list and concludes those rules need a GPU.
//
// What remains, and why it cannot move:
//
//   · MiaoD3D11ParticleRenderer::SelfTest — its *header* includes d3d11.h, so nothing
//     that reaches it can be compiled anywhere else. This is the one case where the
//     dependency really is in the declaration rather than the filing.
//   · MiaoSceneD3D11Renderer::SelfTest — the aggregator, and the only route to
//     TransformMathSelfTest(), which is file-local to MiaoSceneD3D11Renderer.cpp and
//     has no other way in. Calling it also re-runs the content-layer self-tests that
//     ContentSelfTests already covers; that duplication is the price of reaching
//     TransformMathSelfTest, and it is cheap.
//
// Each is reported separately rather than behind one boolean, so a failure names the
// piece that failed instead of "the D3D11 self-test failed".
#include "miaodesk/MiaoD3D11ParticleRenderer.h"
#include "miaodesk/MiaoSceneD3D11Renderer.h"

#include <cstdio>

using namespace miaodesk::content;

int wmain() {
    std::printf("D3D11 渲染器的 Windows-only 自测\n");

    std::printf("\n1. 粒子渲染器(MiaoD3D11ParticleRenderer::SelfTest)\n");
    if (!MiaoD3D11ParticleRenderer::SelfTest()) {
        std::printf("  [FAIL] MiaoD3D11ParticleRenderer::SelfTest 返回 false\n");
        return 1;
    }
    std::printf("  [PASS] 粒子渲染器自测通过\n");

    // The aggregator is the only way to reach TransformMathSelfTest. Its boolean is the
    // whole verdict for that piece plus the seven pure-logic ones.
    std::printf("\n2. 渲染器聚合自测(MiaoSceneD3D11Renderer::SelfTest)\n");
    std::printf("   这也是 TransformMathSelfTest 唯一的入口 —— 它在 "
                "MiaoSceneD3D11Renderer.cpp 里是文件局部的。\n");
    if (!MiaoSceneD3D11Renderer::SelfTest()) {
        std::printf("  [FAIL] MiaoSceneD3D11Renderer::SelfTest 返回 false\n");
        return 1;
    }
    std::printf("  [PASS] TransformMath 与其余自测通过\n");

    std::printf("\nALL CHECKS PASSED\n");
    return 0;
}
