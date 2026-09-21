// Runs the 2D Scene renderer's SelfTest.
//
// This file exists because of a gap found while implementing the textured-sprite draw
// path: MiaoSceneD2DRenderer::SelfTest() is 150+ lines of real assertions — it builds
// a package, renders into a WIC bitmap render target and reads pixels back — and
// nothing in the repository called it. Neither did MiaoSceneD3D11Renderer::SelfTest.
// Code that exists is not verification; a test that never runs is not a test.
//
// So the textured draw path is verified the same way the solid path always could have
// been: a Windows run of the real renderer against a real package, with pixel readback.
// The SelfTest covers both the solid colour path and the textured path, including the
// refusal to apply a non-white tint to a textured sprite.
#include "miaodesk/MiaoD2DTextureLoader.h"
#include "miaodesk/MiaoSceneD2DRenderer.h"

#include <cstdio>

using namespace miaodesk::content;

int wmain() {
    std::printf("\n1. 路径策略(MiaoD2DTextureLoader::SelfTestPathPolicy)\n");
    if (!MiaoD2DTextureLoader::SelfTestPathPolicy()) {
        std::printf("  [FAIL] 路径策略:包内相对路径必须放行,逃逸路径必须拒\n");
        return 1;
    }
    std::printf("  [PASS] 包内相对路径放行,../ 与绝对盘符拒\n");

    // The renderer's SelfTest owns its own package fixture, WIC render target and
    // pixel assertions. Its boolean is the whole verdict.
    std::printf("\n2. Scene 2D 渲染器 SelfTest(纯色 + 贴图两条绘制路径)\n");
    if (!MiaoSceneD2DRenderer::SelfTest()) {
        std::printf("  [FAIL] MiaoSceneD2DRenderer::SelfTest 返回 false\n");
        return 1;
    }
    std::printf("  [PASS] 纯色 sprite、文本、绑定、动画帧需求、贴图 sprite 与贴图 tint 拒绝\n");

    std::printf("\nALL CHECKS PASSED\n");
    return 0;
}
