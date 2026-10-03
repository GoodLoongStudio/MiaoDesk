// 取证样本的**摘要绑定**。
//
// 这不是 RenderEvidenceTest 的重复:那份测判据层怎么拒绝坏样本,这份测产出侧怎么保证
// 样本不会坏。分工的理由是一次真实的回退 ——
// D-1:宿主在 CollectEvidence 里另声明了一个 `std::string digest;`,从没赋值,于是每一帧
// 都带空摘要,`AssessRenderEvidence` 只能整批拒绝。而这一路的单元测试用的是内存端口,
// 端口自己把摘要填对了,所以六条证据用例全绿,真机上 `creator_preview_evidence` 却一次
// 都没成功过。宿主自述"可用",事实是"必然失败"。
//
// 所以这里钉的是产出侧的不变式,而不是判据侧的规则:由 MakeRenderedEvidenceSample
// 构造出来的样本,必须能原样通过判据。判据层与产出层一旦对不上,上面那种
// "测试全绿、真机全败"就会再来一次。
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
const std::string kOtherDigest(64, 'b');

OffscreenEvidenceRequest Canvas() {
    OffscreenEvidenceRequest request;
    request.width = 640;
    request.height = 360;
    request.backend = "d2d";
    request.fixture = "offscreen:640x360";
    return request;
}

RenderEvidenceRequirements Needs() {
    RenderEvidenceRequirements requirements;
    requirements.minFrames = 3;
    return requirements;
}

// 照宿主那一圈的顺序造一批样本:每帧都经过同一个构造点。
std::vector<RenderEvidenceSample> HostStyleBatch(const std::string& digest,
                                                 std::uint32_t frames) {
    const auto request = Canvas();
    std::vector<RenderEvidenceSample> samples;
    for (std::uint32_t index = 0; index < frames; ++index) {
        // 时间按固定步长前进,和宿主用 GetTickCount64 逐帧取值等价。
        samples.push_back(MakeRenderedEvidenceSample(index, 1000 + index * 2, request, digest));
    }
    return samples;
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;

    // 1. 构造点必须把摘要绑到每一帧上。这是 D-1 的直接回归:空摘要的样本整批会被拒,
    //    而拒绝原因说的是"摘要不一致",真正坏掉的那一侧(没算摘要)被那句话盖住。
    const auto batch = HostStyleBatch(kDigest, 3);
    Check(batch.size() == 3, "构造出与请求帧数相同的样本。");
    for (const auto& sample : batch) {
        Check(sample.digest == kDigest, "每一帧都绑上了候选摘要,没有一帧是空的。");
        Check(sample.status == RenderEvidenceStatus::Rendered, "构造出来的帧是渲染器真实输出。");
        Check(sample.offscreen, "构造出来的帧标记为离屏采集。");
        Check(sample.backend == "d2d", "后端来自调用方的那一份描述。");
        Check(sample.width == 640 && sample.height == 360, "画布尺寸来自调用方的那一份描述。");
        Check(sample.fixture == "offscreen:640x360", "fixture 来自调用方的那一份描述。");
    }

    // 2. 产出侧的样本必须原样通过判据层。判据与产出对不上,才是"测试绿、真机败"的成因。
    const auto verdict = AssessRenderEvidence(batch, Needs(), kDigest);
    Check(verdict.acceptable, "由产出侧构造的样本通过判据层:" + verdict.reason);
    Check(verdict.usableFrames == 3, "三帧都被算作真实输出。");
    Check(!verdict.containsSubstitution, "没有占位帧。");

    // 3. 摘要换一个,判据必须拒绝 —— 证明第 2 条不是判据对谁都放行。
    const auto other = AssessRenderEvidence(batch, Needs(), kOtherDigest);
    Check(!other.acceptable, "换一个候选摘要时判据拒绝这批帧。");

    // 4. 帧序号与时间按构造顺序排列,时间必须前进:不前进说明采集卡住了。
    for (std::size_t i = 0; i < batch.size(); ++i) {
        Check(batch[i].frameIndex == i, "帧序号按采集顺序递增。");
        if (i > 0) {
            Check(batch[i].capturedAtMs > batch[i - 1].capturedAtMs, "相邻两帧的时间前进。");
        }
    }

    // 5. 宿主没算摘要时,产出侧自己就必须说清原因,而不是产出一个带空摘要的帧。
    //    它的后果必须是判据能看见的失败,不是一句指错方向的"摘要不一致"。
    const auto empty = MakeRenderedEvidenceSample(0, 1000, Canvas(), "");
    Check(empty.status == RenderEvidenceStatus::Failed,
          "没有候选摘要时产出的是失败帧,不是一个看起来正常的样本。");
    Check(!empty.failureReason.empty(), "失败帧带上了原因,宿主能原样告诉用户。");
    Check(!CountsAsRealOutput(empty.status), "空摘要那一帧不计入可用帧。");
    const auto withEmpty = AssessRenderEvidence(std::vector<RenderEvidenceSample>{empty},
                                                Needs(), "");
    Check(!withEmpty.acceptable, "空摘要那一批判据拒绝。");

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("证据样本摘要绑定:全部 %d 项检查通过\n", g_checks);
    return 0;
}
