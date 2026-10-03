#pragma once

// CCA-08:渲染证据的记录与判定(纯逻辑)。
//
// 计划验收原文:「采集的是渲染器真实输出」「样本绑定正确摘要与时间」「缺素材或渲染
// 失败不会用封面图替代成功」「不需要抓取私人桌面」。
//
// 四条里最容易被绕过去的是第三条,所以它在这里是一个**独立的状态**而不是一个布尔:
// 把渲染失败的样本退回一张内置封面图,是最省事的"让用户看到点什么"的做法,而它的
// 后果是一个坏包看起来渲染得很好。所以 `PlaceholderSubstituted` 与 `Rendered` 分开,
// 且前者永远不计入可用帧。
//
// 另外三条各自对应一种会真实发生的错:
//   * 样本绑定正确摘要 —— 拿上一版的截图冒充这一版,是所有"看起来成功了"里最隐蔽的;
//   * 样本绑定正确时间 —— 时间不前进的样本说明采集卡住了,那不是证据;
//   * 不需要抓取私人桌面 —— 采集必须来自离屏目标,所以每个样本都要说清它是什么后端、
//     由谁产出。屏幕上有什么不该出现在这里。
//
// 它不碰盘、不 import Windows 头,于是这些规则在本机就能真验,而且不需要一个 GPU。
#include <cstdint>
#include <string>
#include <vector>

namespace miaodesk::creator {

enum class RenderEvidenceStatus {
    Rendered,                // 渲染器真实输出了这一帧的像素
    Failed,                  // 渲染失败(缺素材、shader 编译失败、后端不可用…)
    PlaceholderSubstituted,  // 用非候选内容占了位(封面图、纯色、上一版的截图)
    NotAttempted,            // 根本没尝试
};

const char* ToString(RenderEvidenceStatus status) noexcept;
// 这个状态算不算"渲染器真的输出了像素"。只有它算 —— 其余三种都不算,
// 而把它们混同正是"坏包看起来很好"的来路。
bool CountsAsRealOutput(RenderEvidenceStatus status) noexcept;

struct RenderEvidenceSample {
    std::uint32_t frameIndex{};
    std::uint64_t capturedAtMs{};
    std::string backend;          // "d2d" | "d3d11" —— 由产出它的那条路径写
    std::uint32_t width{};
    std::uint32_t height{};
    std::string fixture;          // 设计尺寸 / fixture 名,说明这一帧是在什么画布上采的
    std::string digest;           // 这批像素对应的候选摘要
    RenderEvidenceStatus status{RenderEvidenceStatus::NotAttempted};
    std::string failureReason;
    std::uint64_t byteCount{};    // 宿主写出的 artifact 大小
    std::string artifactPath;
    // 这一帧是在**离屏**目标上采的。为 false 意味着它可能来自用户屏幕上的某个窗口 ——
    // 那既不是候选的真实输出,也会把私人桌面带进 artifact。
    bool offscreen{false};
};

// 采集的限制。默认值与 CreationBudget 里那一套一致:两处必须说同一个数,
// 所以这里提供同一组常量,而不是让它们各自写字面量。
struct RenderEvidenceRequirements {
    std::uint32_t minFrames{1};
    std::uint32_t maxFrames{6};
    std::uint32_t maxEdgePx{1280};
    std::uint64_t maxBytesPerRun{8ull * 1024 * 1024};
    std::uint64_t minFrameSpacingMs{1};   // 相邻两帧至少要隔这么久,否则时间就是假的
};

struct RenderEvidenceVerdict {
    bool acceptable{false};
    std::string reason;
    bool containsFailure{false};      // 有没有失败的帧(失败也是证据,必须报出来)
    bool containsSubstitution{false}; // 有没有用非候选内容占位
    std::uint32_t usableFrames{};
    std::uint64_t totalBytes{};
};

// 这一批样本能不能说明"这个候选确实渲染得出来"。
//
// candidateDigest 是宿主对当前候选算出来的摘要。样本带的摘要与它不一致时,
// 整批都不算 —— 那是上一版的截图,不是这一版的证据。
RenderEvidenceVerdict AssessRenderEvidence(const std::vector<RenderEvidenceSample>& samples,
                                           const RenderEvidenceRequirements& requirements,
                                           std::string_view candidateDigest);

// 离屏采集那一侧的固定描述:宿主用什么后端、在多大的画布、按什么 fixture 名采的。
// 它与"哪一帧"无关,所以单独一个结构,免得每帧重复一遍字面量。
struct OffscreenEvidenceRequest {
    std::uint32_t width{};
    std::uint32_t height{};
    std::string backend;   // "d2d" | "d3d11"
    std::string fixture;   // 设计尺寸 / fixture 名,说明这一帧是在什么画布上采的
};

// 构造一个"渲染器真实输出了这一帧"的样本。
//
// 为什么要有这个函数:宿主侧原来各自赋值 `sample.digest = digest;`,而那个 digest
// 是一个**另外声明**的局部变量。事实是它从来没被赋值过,于是每一帧都带空摘要,
// 判据层只能整批拒绝,而工具却自述"可以用"—— 坏事以"宿主说的"身份出现。
//
// 摘要绑定不住是可以预防的:让它由构造保证。这里只接受一个 candidateDigest,
// 于是"帧属于哪个候选"不可能在调用点写错;后端、画布、fixture、离屏标记与状态也
// 一并写死,宿主那边只剩"画完第几帧、几点画的"。
//
// 空摘要在这里直接拒绝:宿主应当先算摘要并检查可用性,把空摘要放进来的那一侧是错的。
RenderEvidenceSample MakeRenderedEvidenceSample(std::uint32_t frameIndex,
                                                std::uint64_t capturedAtMs,
                                                const OffscreenEvidenceRequest& request,
                                                std::string_view candidateDigest);

} // namespace miaodesk::creator
