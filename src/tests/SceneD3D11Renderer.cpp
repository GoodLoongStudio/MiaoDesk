// Runs the four Windows-only self-tests that sat inside
// MiaoSceneD3D11Renderer::SelfTest() and had no caller at all.
//
// This is P3-6 step 2, and the order it was written in matters. Step 1 moved the four
// D3D11 renderer .cpp into a MiaoDeskSceneD3D11 library; that change had to be proved
// to compile and link under MSVC *before* a test target was allowed to depend on it,
// because otherwise a link failure could not be attributed to either change. That
// proof arrived as green `build` runs, so this target now links the library instead of
// compiling a second copy of its sources.
//
// What is being run, and why each piece is here:
//
//   · MiaoD3D11TextureLoader::SelfTestPathPolicy — which image paths a package may
//     name. Pure policy, but it lives beside D3D11 code and shadows a same-named
//     method on the D2D loader, so "it passes" has meant two different things.
//   · MiaoD3D11ParticleRenderer::SelfTest
//   · MiaoD3D11RenderTargetPool::SelfTest
//   · MiaoSceneD3D11Renderer::SelfTest — the aggregator. It is the *only* route to
//     TransformMathSelfTest(), which is file-local to MiaoSceneD3D11Renderer.cpp and
//     has no other way in. Calling it also re-runs the seven content-layer self-tests
//     that ContentSelfTests already covers on every machine; that duplication is the
//     price of reaching TransformMathSelfTest, and it is cheap.
//
// Each is reported separately rather than behind one boolean, so a failure names the
// piece that failed instead of "the D3D11 self-test failed".
#include "miaodesk/MiaoD3D11ParticleRenderer.h"
#include "miaodesk/MiaoD3D11RenderTarget.h"
#include "miaodesk/MiaoD3D11TextureLoader.h"
#include "miaodesk/MiaoSceneD3D11Renderer.h"

#include <cstdio>

using namespace miaodesk::content;

int wmain() {
    std::printf("D3D11 渲染器的 Windows-only 自测\n");

    std::printf("\n1. 贴图路径策略(MiaoD3D11TextureLoader::SelfTestPathPolicy)\n");
    if (!MiaoD3D11TextureLoader::SelfTestPathPolicy()) {
        std::printf("  [FAIL] 包内相对路径必须放行,逃逸路径必须拒\n");
        return 1;
    }
    std::printf("  [PASS] 包内相对路径放行,../ 与绝对盘符拒\n");

    std::printf("\n2. 粒子渲染器(MiaoD3D11ParticleRenderer::SelfTest)\n");
    if (!MiaoD3D11ParticleRenderer::SelfTest()) {
        std::printf("  [FAIL] MiaoD3D11ParticleRenderer::SelfTest 返回 false\n");
        return 1;
    }
    std::printf("  [PASS] 粒子渲染器自测通过\n");

    std::printf("\n3. 渲染目标池(MiaoD3D11RenderTargetPool::SelfTest)\n");
    if (!MiaoD3D11RenderTargetPool::SelfTest()) {
        std::printf("  [FAIL] MiaoD3D11RenderTargetPool::SelfTest 返回 false\n");
        return 1;
    }
    std::printf("  [PASS] 渲染目标池自测通过\n");

    // The aggregator is the only way to reach TransformMathSelfTest. Its boolean is the
    // whole verdict for that piece plus the seven pure-logic ones.
    std::printf("\n4. 渲染器聚合自测(MiaoSceneD3D11Renderer::SelfTest)\n");
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
