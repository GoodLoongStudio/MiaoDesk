// Runs MiaoSceneSerializer::SelfTest(), which had no caller anywhere in the
// repository.
//
// Why this is worth a file of its own: the D3D11 and D2D renderers each had the same
// problem earlier in this session, and wiring them up immediately caught real bugs.
// A self-test that nothing calls is indistinguishable from one that passes — it just
// never states an opinion. This one is 120 lines of assertions concentrated on particle
// emitters (their id scheme, the 65536-per-emitter and 131072-per-scene budgets that
// `content-review` tells authors to respect, spawn rate, lifetime, sizes, colours,
// material resolution), and it is pure logic, so it runs on every machine.
//
// What it does not cover, so nobody has to guess: it does not touch the Windows-only
// renderers, and `MiaoParticleSerializer::SelfTest` is a stub that returns true without
// asserting anything — the particle *emitter validation* it sits next to is covered
// here, not there. The stub is left alone rather than filled in, because
// `MiaoParticleSerializer::DeserializeEmitters` only forwards to
// `MiaoSceneRuntimeModel::Validate`, which this file already exercises.
#include "miaodesk/MiaoSceneSerializer.h"

#include <cstdio>

using namespace miaodesk::content;

int wmain() {
    std::printf("\n1. MiaoSceneSerializer::SelfTest(此前无任何调用方)\n");
    if (!MiaoSceneSerializer::SelfTest()) {
        std::printf("  [FAIL] MiaoSceneSerializer::SelfTest 返回 false\n");
        std::printf("\nFAILED:1 项断言失败\n");
        return 1;
    }
    std::printf("  [ok]   序列化往返、再序列化稳定性、粒子发射器预算与边界全部通过\n");
    std::printf("\nALL CHECKS PASSED\n");
    return 0;
}
