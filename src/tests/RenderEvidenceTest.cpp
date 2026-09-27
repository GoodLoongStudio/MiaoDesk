// CCA-08:渲染证据的判定。
//
// 计划验收的四条各配一个只违反它的输入:
//   * 采集的是渲染器真实输出 —— 只有 status == Rendered 算,其余三种都不算;
//   * 样本绑定正确摘要 —— 拿上一版的截图冒充这一版,是最隐蔽的"看起来成功了";
//   * 样本绑定正确时间 —— 时间不前进说明采集卡住了,那不是证据;
//   * 缺素材或渲染失败不会用封面图替代成功 —— 占位是独立状态,永远不计入可用帧。
//
// 最后一条是这份测试的重点。把失败帧退回一张内置封面图是最省事的"让用户看到点什么"
// 的做法,而它的后果是一个坏包看起来渲染得很好 —— 所以它必须是一个独立状态,
// 而且必须优先于"帧数够了"被报出来。
#include "miaodesk/RenderEvidence.h"

#include <cstdio>
#include <string>
#include <vector>

namespace miaodesk::creator {
namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL  %s\n", what.c_str());
    }
}

const std::string kDigest(64, 'a');

RenderEvidenceSample Frame(std::uint32_t index, std::uint64_t atMs,
                           RenderEvidenceStatus status = RenderEvidenceStatus::Rendered,
                           const std::string& digest = kDigest) {
    RenderEvidenceSample sample;
    sample.frameIndex = index;
    sample.capturedAtMs = atMs;
    sample.status = status;
    sample.digest = digest;
    sample.backend = "d2d";
    sample.width = 640;
    sample.height = 360;
    sample.fixture = "design:1672x941";
    sample.byteCount = 4096;
    sample.offscreen = true;
    return sample;
}

RenderEvidenceRequirements Needs(std::uint32_t minFrames = 1) {
    RenderEvidenceRequirements requirements;
    requirements.minFrames = minFrames;
    return requirements;
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;

    // --- 1. 采集的是渲染器真实输出 -------------------------------------------
    {
        const std::vector<RenderEvidenceSample> one = {Frame(0, 1000)};
        const auto verdict = AssessRenderEvidence(one, Needs(), kDigest);
        Check(verdict.acceptable, "一帧真实输出即可作为证据");
        Check(verdict.usableFrames == 1, "且它被计为可用帧");

        Check(CountsAsRealOutput(RenderEvidenceStatus::Rendered), "Rendered 算真实输出");
        for (const auto status : {RenderEvidenceStatus::Failed,
                                  RenderEvidenceStatus::PlaceholderSubstituted,
                                  RenderEvidenceStatus::NotAttempted}) {
            Check(!CountsAsRealOutput(status), std::string(ToString(status)) + " 不算");
        }
    }

    // --- 2. 占位不是成功 -----------------------------------------------------
    {
        // 一个只被封面图顶替的候选:帧数够了、尺寸对了、摘要也对,但它不是渲染出来的。
        const std::vector<RenderEvidenceSample> substituted = {
            Frame(0, 1000, RenderEvidenceStatus::PlaceholderSubstituted),
            Frame(1, 2000, RenderEvidenceStatus::PlaceholderSubstituted),
        };
        const auto verdict = AssessRenderEvidence(substituted, Needs(1), kDigest);
        Check(!verdict.acceptable, "用封面图占位的候选不算渲染成功");
        Check(verdict.containsSubstitution, "且它被标记为占位");
        Check(verdict.usableFrames == 0, "占位帧一帧都不计入");
        Check(verdict.reason.find("占位") != std::string::npos, "原因点明是占位");
    }

    // 占位必须优先于"帧数够了"被报出来:有几帧真的、几帧是占位时,
    // 悄悄只报成功的那几帧,等于替候选挑了一份好看的成绩单。
    {
        const std::vector<RenderEvidenceSample> mixed = {
            Frame(0, 1000, RenderEvidenceStatus::Rendered),
            Frame(1, 2000, RenderEvidenceStatus::PlaceholderSubstituted),
        };
        const auto verdict = AssessRenderEvidence(mixed, Needs(1), kDigest);
        Check(!verdict.acceptable, "有占位帧时整批都不算成功");
        Check(verdict.usableFrames == 1, "真实那一帧仍被计为可用");
        Check(verdict.reason.find("占位") != std::string::npos, "但结论是占位");
    }

    // --- 3. 渲染失败 ---------------------------------------------------------
    {
        const std::vector<RenderEvidenceSample> failed = {[] {
            auto sample = Frame(0, 1000, RenderEvidenceStatus::Failed);
            sample.failureReason = "缺素材 assets/cloud.png";
            return sample;
        }()};
        const auto verdict = AssessRenderEvidence(failed, Needs(1), kDigest);
        Check(!verdict.acceptable, "渲染失败不算证据");
        Check(verdict.containsFailure, "且标记为有失败帧");
        Check(verdict.reason.find("失败") != std::string::npos, "原因说明是失败");
    }

    // 失败与成功并存:整批可用,但必须把失败说出来。
    {
        const std::vector<RenderEvidenceSample> mixed = {
            Frame(0, 1000, RenderEvidenceStatus::Rendered),
            [] {
                auto sample = Frame(1, 2000, RenderEvidenceStatus::Failed);
                sample.failureReason = "第二帧时缺素材";
                return sample;
            }(),
        };
        const auto verdict = AssessRenderEvidence(mixed, Needs(1), kDigest);
        Check(verdict.acceptable, "有一帧真实输出就可用");
        Check(verdict.containsFailure, "且不隐瞒有失败帧");
        Check(verdict.reason.find("失败") != std::string::npos, "结论里点明有失败帧");
    }

    // --- 4. 摘要绑定 ---------------------------------------------------------
    {
        const std::vector<RenderEvidenceSample> stale = {Frame(0, 1000, RenderEvidenceStatus::Rendered,
                                                              std::string(64, 'b').c_str())};
        const auto verdict = AssessRenderEvidence(stale, Needs(), kDigest);
        Check(!verdict.acceptable, "摘要不符的样本不算这一版的证据");
        Check(verdict.reason.find("摘要") != std::string::npos, "且说明是摘要不符");

        // 空摘要一律拒绝:放过它等于放过任意候选。
        const auto noDigest = AssessRenderEvidence({Frame(0, 1000)}, Needs(), "");
        Check(!noDigest.acceptable, "没有候选摘要时无法判定归属");
        Check(noDigest.reason.find("摘要") != std::string::npos, "且说明缺摘要");
    }

    // --- 5. 时间绑定 ---------------------------------------------------------
    {
        // 两帧同一时刻:采集卡住了,那不是证据。
        const std::vector<RenderEvidenceSample> stuck = {Frame(0, 1000), Frame(1, 1000)};
        const auto verdict = AssessRenderEvidence(stuck, Needs(1), kDigest);
        Check(!verdict.acceptable, "两帧同一时刻不算证据");
        Check(verdict.reason.find("间隔") != std::string::npos, "原因点明间隔太小");

        // 时间倒流:顺序不可信。
        const std::vector<RenderEvidenceSample> backwards = {Frame(0, 2000), Frame(1, 1000)};
        const auto back = AssessRenderEvidence(backwards, Needs(1), kDigest);
        Check(!back.acceptable, "时间倒流不算证据");
        Check(back.reason.find("更早") != std::string::npos, "且说明时间戳更早");

        // 正常间隔:放行。
        const std::vector<RenderEvidenceSample> spaced = {Frame(0, 1000), Frame(1, 2100)};
        Check(AssessRenderEvidence(spaced, Needs(2), kDigest).acceptable, "正常间隔放行");
    }

    // --- 6. 离屏 -------------------------------------------------------------
    {
        auto onScreen = Frame(0, 1000);
        onScreen.offscreen = false;   // 从用户屏幕上的窗口抓的
        const auto verdict = AssessRenderEvidence({onScreen}, Needs(), kDigest);
        Check(!verdict.acceptable, "非离屏采集的帧被拒");
        Check(verdict.reason.find("离屏") != std::string::npos, "且说明必须离屏");
        Check(verdict.reason.find("用户屏幕") != std::string::npos,
              "并点明它会把私人桌面带进 artifact");
    }

    // --- 7. 尺寸与预算 -------------------------------------------------------
    {
        auto big = Frame(0, 1000);
        big.width = 4000;
        big.height = 3000;
        const auto verdict = AssessRenderEvidence({big}, Needs(), kDigest);
        Check(!verdict.acceptable, "超过边长上限的帧被拒");
        Check(verdict.reason.find("上限") != std::string::npos, "且说明是超过上限");

        const auto zero = AssessRenderEvidence({[] {
            auto sample = Frame(0, 1000);
            sample.width = 0;
            return sample;
        }()}, Needs(), kDigest);
        Check(!zero.acceptable, "尺寸为 0 的帧被拒");

        // 帧数上限。
        std::vector<RenderEvidenceSample> many;
        for (std::uint32_t i = 0; i < 8; ++i) many.push_back(Frame(i, 1000 + i * 100));
        const auto tooMany = AssessRenderEvidence(many, Needs(1), kDigest);
        Check(!tooMany.acceptable, "帧数超过上限被拒");
        Check(tooMany.reason.find("超过上限") != std::string::npos, "且说明是帧数超限");

        // 字节上限。
        std::vector<RenderEvidenceSample> heavy;
        for (std::uint32_t i = 0; i < 3; ++i) {
            auto sample = Frame(i, 1000 + i * 100);
            sample.byteCount = 5ull * 1024 * 1024;
            heavy.push_back(sample);
        }
        const auto tooHeavy = AssessRenderEvidence(heavy, Needs(1), kDigest);
        Check(!tooHeavy.acceptable, "artifact 合计超过上限被拒");
    }

    // --- 8. 至少一帧可用 -----------------------------------------------------
    {
        const auto none = AssessRenderEvidence({}, Needs(1), kDigest);
        Check(!none.acceptable, "一批空样本不算证据");
        Check(none.reason.find("没有采集") != std::string::npos, "且说明没有采集到任何一帧");

        // 要求 3 帧而只有 1 帧真实输出。
        const auto tooFew = AssessRenderEvidence({Frame(0, 1000)}, Needs(3), kDigest);
        Check(!tooFew.acceptable, "可用帧不够时不算证据");
        Check(tooFew.reason.find("没有一帧") != std::string::npos, "且说明没有足够真实输出");
    }

    // --- 9. 上限为 0 表示不设限 ----------------------------------------------
    {
        // 把"没配上限"读成"已经超时"会让任何一帧都过不去。
        RenderEvidenceRequirements unlimited;
        unlimited.minFrames = 1;
        unlimited.maxFrames = 0;
        unlimited.maxEdgePx = 0;
        unlimited.maxBytesPerRun = 0;
        unlimited.minFrameSpacingMs = 0;
        auto huge = Frame(0, 1000);
        huge.width = 9000;
        huge.height = 9000;
        huge.byteCount = 1024ull * 1024 * 1024;
        const auto verdict = AssessRenderEvidence({huge}, unlimited, kDigest);
        Check(verdict.acceptable, "上限为 0 表示不设限");
    }

    std::printf("\nCCA-08 render evidence: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
