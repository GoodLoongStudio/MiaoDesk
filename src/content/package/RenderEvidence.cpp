#include "miaodesk/RenderEvidence.h"

namespace miaodesk::creator {

const char* ToString(RenderEvidenceStatus status) noexcept {
    switch (status) {
    case RenderEvidenceStatus::Rendered: return "Rendered";
    case RenderEvidenceStatus::Failed: return "Failed";
    case RenderEvidenceStatus::PlaceholderSubstituted: return "PlaceholderSubstituted";
    case RenderEvidenceStatus::NotAttempted: return "NotAttempted";
    }
    return "Unknown";
}

bool CountsAsRealOutput(RenderEvidenceStatus status) noexcept {
    return status == RenderEvidenceStatus::Rendered;
}

RenderEvidenceSample MakeRenderedEvidenceSample(std::uint32_t frameIndex,
                                                std::uint64_t capturedAtMs,
                                                const OffscreenEvidenceRequest& request,
                                                std::string_view candidateDigest) {
    RenderEvidenceSample sample;
    // 空摘要不进样本。进来的是空摘要,说明调用方没算摘要,或者算了没检查可用性 ——
    // 这两种情况下放行一帧,判据层之后只会用一句"摘要不一致"把它退回来,而真正的错
    // (宿主没算摘要)被那句拒绝盖住了。所以在产出的这一侧就标成失败,原因写在这里。
    if (candidateDigest.empty()) {
        sample.frameIndex = frameIndex;
        sample.capturedAtMs = capturedAtMs;
        sample.backend = request.backend;
        sample.width = request.width;
        sample.height = request.height;
        sample.fixture = request.fixture;
        sample.status = RenderEvidenceStatus::Failed;
        sample.offscreen = request.backend.empty() ? false : true;
        sample.failureReason = "宿主没有给出候选摘要,这一帧无法绑定到任何候选,不能作为证据。";
        return sample;
    }

    sample.frameIndex = frameIndex;
    sample.capturedAtMs = capturedAtMs;
    sample.backend = request.backend;
    sample.width = request.width;
    sample.height = request.height;
    sample.fixture = request.fixture;
    // 摘要只来自这一个参数。调用点没有第二个字符串可以填,所以"帧属于哪个候选"
    // 不可能在宿主侧写错 —— 这正是上一个版本缺的那一条。
    sample.digest = std::string(candidateDigest);
    sample.status = RenderEvidenceStatus::Rendered;
    sample.offscreen = true;
    return sample;
}

RenderEvidenceVerdict AssessRenderEvidence(const std::vector<RenderEvidenceSample>& samples,
                                           const RenderEvidenceRequirements& requirements,
                                           std::string_view candidateDigest) {
    RenderEvidenceVerdict verdict;

    if (samples.empty()) {
        verdict.reason = "没有采集到任何一帧。";
        return verdict;
    }
    if (candidateDigest.empty()) {
        // 没有候选摘要就无法判断这批像素属于谁。放过一个空摘要等于放过任意候选。
        verdict.reason = "没有候选摘要,无法判断这批证据属于哪个候选。";
        return verdict;
    }

    std::uint64_t previousAt = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& sample = samples[i];

        if (sample.status == RenderEvidenceStatus::Failed) verdict.containsFailure = true;
        if (sample.status == RenderEvidenceStatus::PlaceholderSubstituted) {
            verdict.containsSubstitution = true;
        }

        // 摘要必须逐帧一致。拿上一版的截图冒充这一版,是所有"看起来成功了"里最隐蔽的。
        if (sample.digest != candidateDigest) {
            verdict.reason = "第 " + std::to_string(sample.frameIndex) +
                             " 帧绑定的候选摘要与当前候选不一致 —— 它不是这一版的证据。";
            return verdict;
        }
        // 离屏之外不接受。来自屏幕的帧既不是候选的真实输出,也会把私人桌面带进 artifact。
        if (!sample.offscreen) {
            verdict.reason = "第 " + std::to_string(sample.frameIndex) +
                             " 帧不是离屏采集的。渲染证据必须来自离屏目标,而不是用户屏幕。";
            return verdict;
        }
        // 尺寸上限。超了不只是浪费 artifact 空间 —— 它说明采集路径没有按需求缩放。
        if (requirements.maxEdgePx != 0 &&
            (sample.width > requirements.maxEdgePx || sample.height > requirements.maxEdgePx)) {
            verdict.reason = "第 " + std::to_string(sample.frameIndex) + " 帧的边长 " +
                             std::to_string(sample.width) + "x" + std::to_string(sample.height) +
                             " 超过上限 " + std::to_string(requirements.maxEdgePx) + "。";
            return verdict;
        }
        if (sample.width == 0 || sample.height == 0) {
            verdict.reason = "第 " + std::to_string(sample.frameIndex) + " 帧的尺寸是 0。";
            return verdict;
        }

        // 时间必须前进。两帧同一时刻说明采集卡住了,那不是证据。
        if (i > 0 && sample.capturedAtMs < previousAt) {
            verdict.reason = "第 " + std::to_string(sample.frameIndex) +
                             " 帧的时间戳比上一帧更早 —— 这批样本的顺序不可信。";
            return verdict;
        }
        if (i > 0 && requirements.minFrameSpacingMs != 0 &&
            sample.capturedAtMs - previousAt < requirements.minFrameSpacingMs) {
            verdict.reason = "第 " + std::to_string(sample.frameIndex) +
                             " 帧与上一帧的间隔小于 " +
                             std::to_string(requirements.minFrameSpacingMs) +
                             "ms —— 时间没有真正前进。";
            return verdict;
        }
        previousAt = sample.capturedAtMs;

        verdict.totalBytes += sample.byteCount;
        if (CountsAsRealOutput(sample.status)) ++verdict.usableFrames;
    }

    // 帧数上限。
    if (requirements.maxFrames != 0 && samples.size() > requirements.maxFrames) {
        verdict.reason = "采集了 " + std::to_string(samples.size()) + " 帧,超过上限 " +
                         std::to_string(requirements.maxFrames) + "。";
        return verdict;
    }
    // 字节上限。
    if (requirements.maxBytesPerRun != 0 && verdict.totalBytes > requirements.maxBytesPerRun) {
        verdict.reason = "这一轮采集的 artifact 合计 " + std::to_string(verdict.totalBytes) +
                         " 字节,超过上限 " + std::to_string(requirements.maxBytesPerRun) + "。";
        return verdict;
    }

    // 占位优先报出来。一个被封面图顶替的候选不是"渲染成功",
    // 报成成功会让用户预览到一个他不做出来的东西。
    if (verdict.containsSubstitution) {
        verdict.reason = "这一轮里有帧是用非候选内容占位的,不能当作渲染成功。";
        return verdict;
    }
    if (verdict.usableFrames < requirements.minFrames) {
        if (verdict.containsFailure) {
            verdict.reason = "采集到的帧全部失败了;失败原因见各帧的 failureReason。";
        } else {
            verdict.reason = "没有一帧是渲染器真实输出的。";
        }
        return verdict;
    }

    verdict.acceptable = true;
    verdict.reason = std::to_string(verdict.usableFrames) + " 帧为渲染器真实输出。";
    if (verdict.containsFailure) {
        // 失败与成功并存时仍然算可用,但必须把失败说出来 ——
        // 悄悄只报成功的那几帧,等于替候选挑了一份好看的成绩单。
        verdict.reason += "(这一轮里也有失败的帧,原因见各帧的 failureReason)";
    }
    return verdict;
}

} // namespace miaodesk::creator
