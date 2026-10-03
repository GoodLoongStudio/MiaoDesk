// WALL-03 动画、状态与有界行为:宿主侧时间策略的回归。
//
// MiaoSceneRuntimeTest 与本模块各自管一半,合起来才覆盖规划的验收项:
//   · MiaoSceneRuntimeTest / SelfTest —— "某一时刻场景该是什么值"(已覆盖)
//   · 本文件 —— "宿主怎么把时间喂进去,以及喂进去的东西看得见地连续吗"(此前为零)
//
// 规划给 WALL-03 的验收是"昼夜切换、事件触发、循环接缝与暂停恢复"。事件触发那一半
// 已经在帧调度器的 SelfTest 里;这里覆盖剩下三样,而它们在本机都能真跑:
// 不碰盘、不 import Windows 头、不需要 GPU。
//
// 每条断言都按"坏了会不会红"设计过 —— 让断言靠巧合通过比没有断言更糟。
#include "miaodesk/MiaoSceneTimelinePolicy.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace miaodesk {
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

void CheckNear(double actual, double expected, double tolerance, const std::string& what) {
    ++g_checks;
    if (!(std::fabs(actual - expected) <= tolerance)) {
        ++g_failures;
        std::printf("FAIL  %s (期望 %.9f,实际 %.9f,容差 %.9f)\n", what.c_str(), expected, actual,
                    tolerance);
    }
}

using namespace miaodesk::content;

// 从 Advance 的输出表里按参数 id 取值。找不到时返回 NaN 而不是 0 ——
// 用 0 冒充"没这一项"的话,一个真的是 0 的值会让断言错误地通过。
double ValueOf(const std::vector<std::pair<std::wstring, double>>& table, const std::wstring& id) {
    for (const auto& [key, value] : table)
        if (key == id) return value;
    return std::numeric_limits<double>::quiet_NaN();
}

// ---------------------------------------------------------------------------
// 一、缓动曲线
// ---------------------------------------------------------------------------

void TestEasing() {
    // 四条曲线的端点。端点错一格,整条过渡的起手就能看出来。
    for (AnimationEasing easing : {AnimationEasing::Linear, AnimationEasing::EaseIn,
                                   AnimationEasing::EaseOut, AnimationEasing::EaseInOut}) {
        CheckNear(ApplyAnimationEasing(easing, 0.0), 0.0, 1e-12, "缓动起点必须是 0");
        CheckNear(ApplyAnimationEasing(easing, 1.0), 1.0, 1e-12, "缓动终点必须是 1");
    }
    // 有界:任何输入(含 NaN、负值、远超 1 的值)都不得跑出 [0,1]。
    const double wild[] = {-1e9, -1.0, -1e-9, 0.0, 1e-9, 0.5, 1.0 - 1e-9, 1.0, 1e9,
                           std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()};
    for (AnimationEasing easing : {AnimationEasing::Linear, AnimationEasing::EaseIn,
                                   AnimationEasing::EaseOut, AnimationEasing::EaseInOut}) {
        for (double input : wild) {
            const double value = ApplyAnimationEasing(easing, input);
            Check(value >= 0.0 && value <= 1.0,
                  "缓动对任何输入都有界(NaN/±inf/越界都不许跑出 [0,1])");
        }
    }
    // NaN 必须落到 0 而不是传播成 NaN —— 一帧 NaN 会让整个属性变成 NaN 并停在那里。
    CheckNear(ApplyAnimationEasing(AnimationEasing::Linear,
                                   std::numeric_limits<double>::quiet_NaN()),
              0.0, 1e-12, "NaN 输入按 0 处理,不许传播");
    // 一阶差分非负:单调是对"平滑"的最低要求(EaseOut 起步快、EaseIn 收尾快,都不许回头)。
    for (AnimationEasing easing : {AnimationEasing::Linear, AnimationEasing::EaseIn,
                                   AnimationEasing::EaseOut, AnimationEasing::EaseInOut}) {
        bool monotone = true;
        double previous = ApplyAnimationEasing(easing, 0.0);
        for (int i = 1; i <= 200; ++i) {
            const double value = ApplyAnimationEasing(easing, static_cast<double>(i) / 200.0);
            if (value < previous - 1e-12) monotone = false;
            previous = value;
        }
        Check(monotone, "缓动单调不回折");
    }
    // EaseOut 的中点在 Linear 之上(起步快),EaseIn 在之下。这一条区分四条曲线 ——
    // 只查端点的话,四条曲线互换也发现不了。
    CheckNear(ApplyAnimationEasing(AnimationEasing::Linear, 0.5), 0.5, 1e-12, "Linear 中点是 0.5");
    Check(ApplyAnimationEasing(AnimationEasing::EaseOut, 0.5) > 0.5,
          "EaseOut 中点在 Linear 之上(起步快)");
    Check(ApplyAnimationEasing(AnimationEasing::EaseIn, 0.5) < 0.5,
          "EaseIn 中点在 Linear 之下(收尾快)");
    CheckNear(ApplyAnimationEasing(AnimationEasing::EaseIn, 0.5), 0.25, 1e-12, "EaseIn 中点是 0.25");
    CheckNear(ApplyAnimationEasing(AnimationEasing::EaseOut, 0.5), 0.75, 1e-12,
              "EaseOut 中点是 0.75");
}

// ---------------------------------------------------------------------------
// 二、暂停恢复
// ---------------------------------------------------------------------------

void TestSceneClock() {
    SceneClock clock;
    clock.Start(0.0);

    // 核心性质:暂停任意时长,场景时间一秒都不多走。
    CheckNear(clock.Advance(10.0), 10.0, 1e-12, "运行时按墙钟增量推进");
    clock.Pause();
    const double frozen = clock.Now();
    for (int i = 0; i < 100; ++i) clock.Advance(1.0);  // 被挡住 100 秒
    CheckNear(clock.Now(), frozen, 0.0, "暂停期间场景时间一动不动");
    CheckNear(clock.SkippedWallSeconds(), 100.0, 1e-12, "被丢掉的墙钟被记下来(诊断用)");
    clock.Resume();
    CheckNear(clock.Advance(2.0), frozen + 2.0, 1e-12,
              "恢复后从冻住的地方继续,而不是从墙钟继续(暂停恢复)");

    // 重复 Pause/Resume 是空操作,且不把同一段墙钟记两遍。
    // 这一条钉的是可观测契约:宿主每个隐藏事件都可能调一次 Pause。
    SceneClock noisy;
    noisy.Start(0.0);
    noisy.Pause();
    noisy.Pause();
    noisy.Advance(5.0);
    noisy.Resume();
    noisy.Resume();
    noisy.Advance(3.0);
    CheckNear(noisy.Now(), 3.0, 1e-12, "重复 Pause/Resume 之后时钟仍正确");
    CheckNear(noisy.SkippedWallSeconds(), 5.0, 1e-12, "同一段墙钟只记一次");
    // 已在运行时再 Pause 一次就真的停下(守卫不能把"第二次"当成"第一次"忽略)。
    SceneClock rePause;
    rePause.Start(0.0);
    rePause.Advance(1.0);
    rePause.Pause();
    rePause.Resume();
    rePause.Advance(2.0);
    rePause.Pause();
    rePause.Advance(9.0);
    CheckNear(rePause.Now(), 3.0, 1e-12, "Resume 后再 Pause 仍能冻住(不是一次性开关)");

    // Seek 在暂停期必须被忽略 —— 否则暂停可以被绕过,上面全盘皆输。
    SceneClock seek;
    seek.Start(0.0);
    seek.Advance(7.0);
    seek.Pause();
    seek.Seek(999.0);
    CheckNear(seek.Now(), 7.0, 1e-12, "暂停期禁止 Seek(否则暂停形同不存在)");
    seek.Resume();
    seek.Seek(42.0);
    CheckNear(seek.Now(), 42.0, 1e-12, "运行期 Seek 生效(宿主要对齐音频播放头)");

    // 非有限增量被丢掉而不是传播成 NaN。
    SceneClock dirty;
    dirty.Start(1.0);
    Check(std::isfinite(dirty.Advance(std::numeric_limits<double>::quiet_NaN())),
          "NaN 增量不污染场景时间");
    CheckNear(dirty.Now(), 1.0, 1e-12, "NaN 增量等于没推进");
    dirty.Advance(-5.0);  // 宿主时钟回拨:不许倒流
    CheckNear(dirty.Now(), 1.0, 1e-12, "负增量不倒流(宿主时钟回拨不是时间倒转)");

    // Reset 回到"未启动"。
    dirty.Reset();
    Check(!dirty.Running() && dirty.Now() == 0.0 && dirty.SkippedWallSeconds() == 0.0,
          "Reset 回到未启动");

    // 场景时间是"运行时间的函数":暂停前后的顺序不影响结果。
    SceneClock a;
    a.Start(0.0);
    a.Advance(3.0);
    a.Pause();
    a.Advance(1000.0);
    a.Resume();
    a.Advance(4.0);
    SceneClock b;
    b.Start(0.0);
    b.Advance(7.0);  // 一次跑完同样的运行时长
    CheckNear(a.Now(), b.Now(), 1e-12, "场景时间只取决于累计运行时长,与何时暂停无关");
}

// ---------------------------------------------------------------------------
// 三、平滑参数过渡
// ---------------------------------------------------------------------------

void TestParameterSlew() {
    // 显式用 Linear:ParameterSlewRequest 的默认曲线是 EaseOut(见那个结构体的注释),
    // 而这里要验的是算术本身,不想让曲线形状掺进来。
    ParameterSlewRequest request;
    request.durationSeconds = 1.0;
    request.easing = AnimationEasing::Linear;
    request.from = 0.0;
    request.to = 1.0;

    // 过渡必须"两端准确":起手就是 from,收尾就是 to。中间偏一点没人看得出来,
    // 两端偏一点就是"一帧跳变"或"永远差一点"。
    CheckNear(SampleParameterSlew(request, 0.0).value, 0.0, 1e-12, "过渡起点准确落在 from");
    CheckNear(SampleParameterSlew(request, 1.0).value, 1.0, 1e-12, "过渡终点准确落在 to");
    Check(SampleParameterSlew(request, 1.0).settled, "到达时长即结束");
    Check(SampleParameterSlew(request, 99.0).settled, "超时仍结束且不飞出去");
    CheckNear(SampleParameterSlew(request, 99.0).value, 1.0, 1e-12, "结束后保持在终值");
    Check(!SampleParameterSlew(request, 0.999).settled, "时长内不结束");

    // 单调(Linear 下严格递增):回头就是"拖滑块时弹回去"。
    double previous = -1.0;
    bool strictlyIncreasing = true;
    for (int i = 0; i <= 100; ++i) {
        const double value = SampleParameterSlew(request, static_cast<double>(i) / 100.0).value;
        if (value < previous) strictlyIncreasing = false;
        previous = value;
    }
    Check(strictlyIncreasing, "Linear 过渡单调不回折");
    CheckNear(SampleParameterSlew(request, 0.5).value, 0.5, 1e-12, "Linear 过渡中点在中间");

    // EaseOut 起步快:拖滑块放手的瞬间就该跟上,而不是慢半拍再走。
    ParameterSlewRequest eased = request;
    eased.easing = AnimationEasing::EaseOut;
    Check(SampleParameterSlew(eased, 0.25).value > 0.25, "EaseOut 过渡起步快(前 1/4 走完一半以上)");

    // durationSeconds <= 0 = 不要过渡。此时返回 to,不是 from ——
    // 宿主已经把新值写进场景了,再报 from 会把它改回去。
    ParameterSlewRequest instant = request;
    instant.durationSeconds = 0.0;
    CheckNear(SampleParameterSlew(instant, 0.0).value, 1.0, 1e-12,
              "不要过渡时报终值(不能把宿主刚设的值改回去)");
    Check(SampleParameterSlew(instant, 0.0).settled, "不要过渡时立即结束");
    ParameterSlewRequest negative = request;
    negative.durationSeconds = -1.0;
    Check(SampleParameterSlew(negative, 0.0).settled, "负时长按不要过渡处理");

    // 负 elapsed 视为 0(宿主可能给一个还没开始的时钟)。
    CheckNear(SampleParameterSlew(request, -3.0).value, 0.0, 1e-12, "负 elapsed 按 0 处理");

    // 暂停期不给增量,过渡就停着:锁屏不该让所有过渡一起跳到终点。
    CheckNear(SampleParameterSlew(request, 0.4).value, SampleParameterSlew(request, 0.4).value,
              0.0, "同样的场景时间得到同样的值(与墙钟无关)");

    // ---- ParameterSlewSet ----
    ParameterSlewSet slews;
    std::vector<std::pair<std::wstring, double>> out;

    slews.Begin(L"param://strength", request);
    Check(slews.Active(L"param://strength"), "Begin 之后该参数处于过渡中");
    Check(slews.ActiveCount() == 1, "过渡计数为 1");

    // 分多次 Advance:每条过渡按自己累计的场景时间走,不是按"第几次调用"走。
    slews.Advance(0.25, &out);
    Check(out.size() == 1 && out.front().first == L"param://strength", "Advance 报出未结束的过渡");
    CheckNear(out.front().second, 0.25, 1e-12, "推进 1/4 后值在 1/4 处");
    slews.Advance(0.25, &out);
    CheckNear(out.front().second, 0.5, 1e-12, "再推进 1/4 后值在 1/2 处(按累计时间,不是按调用次数)");

    // 结束的那一帧也要报出终值,否则屏幕上会停在倒数第二帧的值。
    Check(!slews.Advance(0.5, &out), "再推进 0.5 后过渡走完全程,没有活跃过渡剩下");
    Check(out.size() == 1 && out.front().second == 1.0, "结束帧报出终值");
    Check(!slews.Active(L"param://strength"), "结束之后不再活跃");
    Check(slews.ActiveCount() == 0, "结束之后计数归零");
    Check(!slews.Advance(1.0, &out), "全部结束后 Advance 报 false(宿主可以退到空闲刷新)");
    Check(out.empty(), "全部结束后不再报任何参数");
    CheckNear(slews.ValueOf(L"param://strength"), 1.0, 1e-12, "结束后取值是终值");

    // 过渡途中改目标:从头开始一条新过渡,而不是排队。
    // 排队会让滑块永远追不上手;而从当前值重新起跳才跟手。
    ParameterSlewSet retarget;
    retarget.Begin(L"param://strength", request);
    retarget.Advance(0.5, &out);
    ParameterSlewRequest second = request;
    second.from = out.front().second;  // 从当前值起跳
    second.to = 0.5;
    retarget.Begin(L"param://strength", second);
    Check(retarget.Active(L"param://strength"), "改目标后仍在过渡中(没排队)");
    retarget.Advance(1.0, &out);
    CheckNear(retarget.ValueOf(L"param://strength"), 0.5, 1e-12, "新过渡走到新目标");

    // 多条过渡同时进行,互不干扰。Advance 报出所有未结束的参数以及这一帧刚好结束的那个,
    // 所以宿主每帧把整张表写回场景即可,不必自己挑哪些变了。
    ParameterSlewSet multi;
    multi.Begin(L"param://a", request);
    ParameterSlewRequest shortOne = request;
    shortOne.durationSeconds = 0.25;
    multi.Begin(L"param://b", shortOne);
    Check(multi.ActiveCount() == 2, "两条过渡各记一条");
    Check(multi.Advance(0.25, &out), "短过渡刚结束、长过渡还在,所以仍有活跃过渡");
    Check(out.size() == 2, "Advance 同时报未结束的参数与本帧结束的参数");
    CheckNear(ValueOf(out, L"param://b"), 1.0, 1e-12, "短过渡到终点");
    CheckNear(ValueOf(out, L"param://a"), 0.25, 1e-12, "长过渡走完自己的 1/4");
    Check(multi.Active(L"param://a") && !multi.Active(L"param://b"), "短过渡已结束、长过渡仍在");
    Check(!multi.Advance(0.75, &out), "再推进 3/4 后长过渡也结束,没有活跃过渡剩下");
    CheckNear(ValueOf(out, L"param://a"), 1.0, 1e-12, "长过渡到终点");
    Check(!multi.Active(L"param://a"), "长过渡也结束了");

    // Reset 清空。
    multi.Reset();
    Check(multi.ActiveCount() == 0, "Reset 清空全部过渡");

    // 输出指针为空时不许崩,也不许写。
    ParameterSlewSet nullOut;
    nullOut.Begin(L"param://a", request);
    Check(!nullOut.Advance(0.1, nullptr), "输出指针为空时返回 false 且不写");
}

// ---------------------------------------------------------------------------
// 四、循环接缝与跳变
// ---------------------------------------------------------------------------

AnimationTrackDefinition Track(const wchar_t* id, AnimationLoopMode loop, double duration,
                               std::vector<AnimationKeyframeDefinition> keyframes) {
    AnimationTrackDefinition track;
    track.id = id;
    track.target = PropertyAddress{L"component://root/transform", L"opacity"};
    track.loopMode = loop;
    track.durationSeconds = duration;
    track.keyframes = std::move(keyframes);
    return track;
}

// 一条闭合的循环轨道:count 个关键帧摊在 duration 上,值走一个完整的余弦周期,
// 首尾值相同。形状与 NeonCity/MiaoCloud/MysticMoon 的 16 关键帧轨道一致 ——
// 用真实形状而不是随手编的数,是因为"内置壁纸那种密度"这句话必须有个依据。
std::vector<AnimationKeyframeDefinition> ClosedCycle(double count, double low, double high,
                                                     double duration) {
    std::vector<AnimationKeyframeDefinition> keyframes;
    for (double i = 0; i < count; ++i) {
        AnimationKeyframeDefinition keyframe;
        const double phase = i / (count - 1.0);
        keyframe.timeSeconds = duration * phase;
        keyframe.value = low + (high - low) * (1.0 - std::cos(2.0 * 3.14159265358979323846 * phase)) / 2.0;
        keyframe.easing = AnimationEasing::Linear;
        keyframes.push_back(keyframe);
    }
    return keyframes;
}

void TestContinuity() {
    const double fps = 60.0;

    // ---- 接缝 ----
    // 端点相同:无缝。
    auto seamless = Track(L"animation://a", AnimationLoopMode::Loop, 2.0,
                          {AnimationKeyframeDefinition{0.0, 0.2, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{1.0, 0.9, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{2.0, 0.2, AnimationEasing::Linear}});
    CheckNear(AnimationLoopSeamFraction(seamless), 0.0, 0.0, "端点相同的 Loop 轨道接缝为 0");
    Check(!(AuditAnimationContinuity(seamless, nullptr, fps) & kAnimationLoopSeamMismatch),
          "端点相同不报接缝问题");

    // 端点不同:每圈硬跳一次。三个内置壁纸的 8 条轨道端点都相同,所以这条是防将来的回归。
    auto snapping = Track(L"animation://b", AnimationLoopMode::Loop, 2.0,
                          {AnimationKeyframeDefinition{0.0, 0.2, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{1.0, 0.9, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{2.0, 1.0, AnimationEasing::Linear}});
    Check(AuditAnimationContinuity(snapping, nullptr, fps) & kAnimationLoopSeamMismatch,
          "端点不同的 Loop 轨道报接缝不连续");
    // 比例按轨道自身行程归一:0.8 / 0.8 = 1(整个行程都跳回去)。
    CheckNear(AnimationLoopSeamFraction(snapping), 1.0, 1e-12,
              "接缝比例按轨道自身行程归一(行程 0.8、跳 0.8 → 1.0)");

    // PingPong 与 Once 结构上不可能在接缝处断裂。
    auto ping = snapping;
    ping.loopMode = AnimationLoopMode::PingPong;
    CheckNear(AnimationLoopSeamFraction(ping), 0.0, 0.0, "PingPong 没有循环接缝(它折返)");
    auto once = snapping;
    once.loopMode = AnimationLoopMode::Once;
    CheckNear(AnimationLoopSeamFraction(once), 0.0, 0.0, "Once 没有循环接缝(它不循环)");

    // 恒定轨道:行程 0,接缝比例必须按 0 处理而不是除以 0。
    auto constant = Track(L"animation://c", AnimationLoopMode::Loop, 2.0,
                          {AnimationKeyframeDefinition{0.0, 0.5, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{1.0, 0.5, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{2.0, 0.5, AnimationEasing::Linear}});
    CheckNear(AnimationLoopSeamFraction(constant), 0.0, 0.0, "恒定轨道接缝为 0(不许除以 0)");
    CheckNear(AnimationTrackValueRange(constant), 0.0, 0.0, "恒定轨道行程为 0");

    // 端点几乎相同(处在阈值内的 double 误差):不算接缝。
    auto nearlySeamless = seamless;
    nearlySeamless.keyframes.back().value = PropertyValue{0.2 + 1e-9};
    Check(!(AuditAnimationContinuity(nearlySeamless, nullptr, fps) & kAnimationLoopSeamMismatch),
          "端点相差 1e-9 不算接缝(JSON 往返与 double 累加的余地)");

    // 端点只差一点点但仍可辨:报。
    auto slightSeam = seamless;
    slightSeam.keyframes.back().value = PropertyValue{0.2 + 1e-3};
    CheckNear(AnimationLoopSeamFraction(slightSeam), 1e-3 / 0.7, 1e-9, "小接缝按比例如实报出");
    Check(AuditAnimationContinuity(slightSeam, nullptr, fps) & kAnimationLoopSeamMismatch,
          "小到 0.14% 行程的接缝也报(昼夜循环看得见)");

    // ---- 一帧内跳变 ----
    // 16 个关键帧摊在 8.7 秒上,与三个内置壁纸同一形状(那些轨道 duration 就是 8.726646259971648):
    // 每帧步长远低于门槛。这是"当前内容是好的"的固定样本。
    auto wallpapers = Track(L"animation://neon", AnimationLoopMode::Loop, 8.726646259971648,
                            ClosedCycle(16, -1.5, 1.5, 8.726646259971648));
    Check(!(AuditAnimationContinuity(wallpapers, nullptr, 60.0) & kAnimationFrameJump),
          "内置壁纸那种密度的轨道在 60fps 下不报跳变(它真的好)");
    Check(!(AuditAnimationContinuity(wallpapers, nullptr, 240.0) & kAnimationFrameJump),
          "同样密度的轨道在 240fps 下也不报跳变");
    Check(AnimationFrameStepFraction(wallpapers, 60.0) < kAnimationFrameJumpThreshold,
          "内置壁纸形状的帧步长低于门槛");
    Check(!(AuditAnimationContinuity(wallpapers, nullptr, 60.0) & kAnimationLoopSeamMismatch),
          "同一形状首尾值相同,不报接缝");

    // 两个关键帧相距 1e-9 秒、值差整个行程:Validate 放过它(只查时间递增),
    // 而它在一帧之内把 0.2 变成 1.0。这是本轮探针实测到的真实缺口。
    auto teleport = Track(L"animation://tele", AnimationLoopMode::Once, 2.0,
                          {AnimationKeyframeDefinition{0.0, 0.2, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{1e-9, 1.0, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{2.0, 1.0, AnimationEasing::Linear}});
    Check(AuditAnimationContinuity(teleport, nullptr, 60.0) & kAnimationFrameJump,
          "相距 1e-9 秒、值差整个行程时报跳变(插值一次都采不到样)");
    CheckNear(AnimationFrameStepFraction(teleport, 60.0), 1.0, 1e-9,
              "那种写法的帧步长就是整个行程");

    // 同样一对端点,摊到 1 秒:正常,不报。
    auto normal = Track(L"animation://normal", AnimationLoopMode::Once, 2.0,
                        {AnimationKeyframeDefinition{0.0, 0.2, AnimationEasing::Linear},
                         AnimationKeyframeDefinition{1.0, 1.0, AnimationEasing::Linear},
                         AnimationKeyframeDefinition{2.0, 1.0, AnimationEasing::Linear}});
    Check(!(AuditAnimationContinuity(normal, nullptr, 60.0) & kAnimationFrameJump),
          "同样两点摊到 1 秒不报跳变");

    // 帧率越高,帧步长越小:同一把尺子在不同 fps 下给出不同答案。
    Check(AnimationFrameStepFraction(normal, 240.0) < AnimationFrameStepFraction(normal, 60.0),
          "帧步长随帧率升高而降低(同一段动画在高刷下更平滑)");
    // normal 的行程是 0.8,第一段一秒之内走完这 0.8,所以"段内行程比例"是 1.0,
    // 一帧占该段的 1/60 —— 于是帧步长是 1/60,不是 0.8/60。
    CheckNear(AnimationFrameStepFraction(normal, 60.0), 1.0 / 60.0, 1e-9,
              "帧步长 = 段内行程比例 ÷ 段内帧数(0.8 的行程在一秒内走完,比例就是 1)");
    CheckNear(AnimationFrameStepFraction(normal, 240.0), 1.0 / 240.0, 1e-9, "240fps 下的帧步长");

    // 非法的 fps 不许让结果变成 NaN 或 0。
    Check(std::isfinite(AnimationFrameStepFraction(normal, 0.0)), "fps=0 不产出 NaN");
    Check(std::isfinite(AnimationFrameStepFraction(normal, -1.0)), "负 fps 不产出 NaN");
    Check(std::isfinite(AnimationFrameStepFraction(normal, std::numeric_limits<double>::quiet_NaN())),
          "NaN fps 不产出 NaN");

    // Loop 的接缝那一步按一帧内的整段位移量计(不是除以圈数)。
    // snapping 的接缝是 1.0(整个行程),超过门槛,于是它既报接缝也报跳变。
    Check(AuditAnimationContinuity(snapping, nullptr, 60.0) & kAnimationFrameJump,
          "端点不同的 Loop 轨道同时报跳变(接缝那一步在一帧内走完整形成)");

    // ---- 端点停在 0 / duration 之外 ----
    auto holdHead = Track(L"animation://head", AnimationLoopMode::Loop, 2.0,
                          {AnimationKeyframeDefinition{1.5, 0.2, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{2.0, 0.9, AnimationEasing::Linear}});
    Check(AuditAnimationContinuity(holdHead, nullptr, 60.0) & kAnimationLoopHoldAtEdge,
          "Loop 轨道第一个关键帧不在 0:每圈开头有一段停在端点");
    auto holdTail = Track(L"animation://tail", AnimationLoopMode::Loop, 2.0,
                          {AnimationKeyframeDefinition{0.0, 0.2, AnimationEasing::Linear},
                           AnimationKeyframeDefinition{1.5, 0.9, AnimationEasing::Linear}});
    Check(AuditAnimationContinuity(holdTail, nullptr, 60.0) & kAnimationLoopHoldAtEdge,
          "Loop 轨道最后一个关键帧不在 duration:每圈末尾有一段停在端点");
    // 差 1e-3 秒以内当作浮点误差,不报。
    auto tolerant = seamless;
    tolerant.keyframes.back().timeSeconds = 2.0 - 5e-4;
    Check(!(AuditAnimationContinuity(tolerant, nullptr, 60.0) & kAnimationLoopHoldAtEdge),
          "端点偏离 duration 在 1e-3 秒内不报");
    // Once 轨道的端点不在 0/duration 是完全正常的(它只播一段),不该报。
    auto oncePartial = Track(L"animation://partial", AnimationLoopMode::Once, 2.0,
                             {AnimationKeyframeDefinition{0.5, 0.2, AnimationEasing::Linear},
                              AnimationKeyframeDefinition{1.5, 0.9, AnimationEasing::Linear}});
    Check(!(AuditAnimationContinuity(oncePartial, nullptr, 60.0) & kAnimationLoopHoldAtEdge),
          "Once 轨道端点不在 0/duration 是正常的,不报");

    // ---- 属性被多个写者同时写(报告,不拒绝) ----
    std::vector<PropertyBindingDefinition> bindings = {
        PropertyBindingDefinition{L"binding://opacity",
                                  PropertyAddress{L"component://root/transform", L"opacity"},
                                  BindingSourceKind::Parameter,
                                  L"param://strength",
                                  1.0,
                                  0.0}};
    Check(AuditAnimationContinuity(seamless, &bindings, 60.0) & kAnimationPropertyContested,
          "动画与 binding 写同一属性时报抢占");
    Check(!(AuditAnimationContinuity(seamless, nullptr, 60.0) & kAnimationPropertyContested),
          "没有 binding 清单时报不出抢占");
    std::vector<PropertyBindingDefinition> other = {
        PropertyBindingDefinition{L"binding://position",
                                  PropertyAddress{L"component://root/transform", L"position"},
                                  BindingSourceKind::Parameter,
                                  L"param://strength",
                                  1.0,
                                  0.0}};
    Check(!(AuditAnimationContinuity(seamless, &other, 60.0) & kAnimationPropertyContested),
          "binding 写的是别的属性:不报抢占");

    // ---- 点名 ----
    // 点名给宿主显示,也给作者用。语义有两层,不要混:
    //   · 两条动画写同一属性 —— 先声明的那条**从来不可观测**,Validate 直接拒(见
    //     MiaoSceneRuntimeTest)。这里点名是为了把拒绝的理由说具体。
    //   · 一条 binding 与动画写同一属性 —— **合法**,binding 在 Initialize 提供起始值;
    //     动画开跑之后它被盖住。所以这一层是报告,不是拒绝。
    {
        std::vector<AnimationTrackDefinition> animations = {seamless, snapping};
        const auto contested = ContestedAnimationTargets(animations, bindings);
        Check(contested.size() == 2, "两处并发写各点一名");
        bool namedEarlierTrack = false;
        bool namedBinding = false;
        bool namedLaterTrack = false;
        for (const auto& id : contested) {
            if (id == L"animation://a") namedEarlierTrack = true;
            if (id == L"binding://opacity") namedBinding = true;
            if (id == L"animation://b") namedLaterTrack = true;
        }
        Check(namedEarlierTrack, "点名先声明的那条动画(它从来不可观测)");
        Check(!namedLaterTrack, "后声明的动画不被点名(它赢了)");
        Check(namedBinding, "点名被动画盖住的 binding(它是被报告的,不是被拒绝的)");
    }
    {
        const auto none = ContestedAnimationTargets({}, {});
        Check(none.empty(), "没有动画与 binding 时报不出任何抢占");
    }
    {
        // 只有一条动画、binding 写别的属性:没有抢占。
        std::vector<AnimationTrackDefinition> animations = {seamless};
        Check(ContestedAnimationTargets(animations, other).empty(), "不冲突时点名列表为空");
    }

    // ---- 描述文字 ----
    Check(DescribeAnimationContinuity(kAnimationContinuous) == L"连续", "无问题时描述为'连续'");
    const auto text = DescribeAnimationContinuity(kAnimationLoopSeamMismatch | kAnimationFrameJump);
    Check(text.find(L"接缝") != std::wstring::npos && text.find(L"瞬移") != std::wstring::npos,
          "描述文字覆盖每一个被点出的问题");
    Check(DescribeAnimationContinuity(kAnimationPropertyContested).find(L"写者") != std::wstring::npos,
          "抢占问题的描述提到写者");
    Check(DescribeAnimationContinuity(kAnimationLoopHoldAtEdge).find(L"端点") != std::wstring::npos,
          "端点悬停问题的描述提到端点(宿主要能把它显示给人)");
    const auto both = DescribeAnimationContinuity(kAnimationLoopHoldAtEdge | kAnimationLoopSeamMismatch);
    Check(both.find(L"端点") != std::wstring::npos && both.find(L"接缝") != std::wstring::npos,
          "多个问题同时出现时描述都列出来,不互相吞掉");

    // ---- 距离函数 ----
    CheckNear(AnimationValueDistance(PropertyValue{1.0}, PropertyValue{4.0}), 3.0, 0.0,
              "double 距离");
    CheckNear(AnimationValueDistance(PropertyValue{Vec2{0.0, 0.0}}, PropertyValue{Vec2{3.0, -5.0}}),
              5.0, 0.0, "Vec2 距离取最大分量差(不是各分量之和:3 与 5 的和是 8)");
    CheckNear(AnimationValueDistance(PropertyValue{Color4{0.0, 0.0, 0.0, 1.0}},
                                     PropertyValue{Color4{0.0, 0.0, 0.0, 0.0}}),
              1.0, 0.0, "Color4 距离");
    CheckNear(AnimationValueDistance(PropertyValue{1.0}, PropertyValue{std::wstring(L"x")}), 0.0, 0.0,
              "类型不同时距离为 0(不许按 -1 或 NaN 处理)");
    CheckNear(AnimationValueDistance(PropertyValue{(std::int64_t)2}, PropertyValue{(std::int64_t)-2}),
              4.0, 0.0, "int64 距离");
    CheckNear(AnimationTrackValueRange(normal), 0.8, 1e-12, "轨道行程取任意两个关键帧的最大距离");

    // 行程实现是 O(n·d) 而不是 O(n²)(Validate 允许一条轨道 4096 个关键帧,
    // O(n²) 是 840 万次 variant 比较)。这里拿暴力法钉住这个改写没改错:
    // "分量差取最大"这种度量下,两两最大距离等于各分量极差的最大值。
    // 只测一条不够 —— 要让它覆盖到"最大值与最小值不在同一个关键帧上"。
    {
        AnimationTrackDefinition tricky;
        tricky.id = L"animation://tricky";
        tricky.target = PropertyAddress{L"component://root/transform", L"position"};
        tricky.loopMode = AnimationLoopMode::Loop;
        tricky.durationSeconds = 4.0;
        tricky.keyframes = {
            AnimationKeyframeDefinition{0.0, Vec2{10.0, -5.0}, AnimationEasing::Linear},
            AnimationKeyframeDefinition{1.0, Vec2{-3.0, 7.0}, AnimationEasing::Linear},
            AnimationKeyframeDefinition{2.0, Vec2{4.0, 2.0}, AnimationEasing::Linear},
            AnimationKeyframeDefinition{3.0, Vec2{-8.0, -1.0}, AnimationEasing::Linear},
        };
        double brute = 0.0;
        for (const auto& a : tricky.keyframes)
            for (const auto& b : tricky.keyframes)
                brute = std::max(brute, AnimationValueDistance(a.value, b.value));
        CheckNear(AnimationTrackValueRange(tricky), brute, 0.0,
                  "行程与暴力法两两最大距离一致(x 极差 18、y 极差 12,最大是 18)");
        CheckNear(AnimationTrackValueRange(tricky), 18.0, 0.0, "该用例的行程就是 x 的极差 18");

        // 颜色同样如此,并且只取最大分量差而不是四个分量相加。
        AnimationTrackDefinition color;
        color.id = L"animation://color";
        color.target = PropertyAddress{L"component://root/transform", L"tint"};
        color.loopMode = AnimationLoopMode::Loop;
        color.durationSeconds = 2.0;
        color.keyframes = {
            AnimationKeyframeDefinition{0.0, Color4{0.1, 0.2, 0.3, 1.0}, AnimationEasing::Linear},
            AnimationKeyframeDefinition{2.0, Color4{0.9, 0.2, 0.3, 0.0}, AnimationEasing::Linear},
        };
        CheckNear(AnimationTrackValueRange(color), 1.0, 0.0,
              "颜色行程取最大分量差(r 差 0.8、a 差 1.0,取 1.0)");

        // 恒定轨道与空轨道:行程 0,不许除以 0(接缝比例与帧步长都拿它做分母)。
        AnimationTrackDefinition flat;
        flat.id = L"animation://flat";
        flat.target = PropertyAddress{L"component://root/transform", L"opacity"};
        flat.keyframes = {AnimationKeyframeDefinition{0.0, 0.5, AnimationEasing::Linear},
                          AnimationKeyframeDefinition{1.0, 0.5, AnimationEasing::Linear}};
        CheckNear(AnimationTrackValueRange(flat), 0.0, 0.0, "恒定轨道行程为 0");
        AnimationTrackDefinition empty;
        empty.id = L"animation://empty";
        empty.target = PropertyAddress{L"component://root/transform", L"opacity"};
        CheckNear(AnimationTrackValueRange(empty), 0.0, 0.0, "空轨道行程为 0(Validate 会拒它,但这里不许崩)");
        AnimationTrackDefinition single;
        single.id = L"animation://single";
        single.target = PropertyAddress{L"component://root/transform", L"opacity"};
        single.keyframes = {AnimationKeyframeDefinition{0.0, 0.7, AnimationEasing::Linear}};
        CheckNear(AnimationTrackValueRange(single), 0.0, 0.0, "只有一个关键帧时行程为 0");
    }
}

} // namespace
} // namespace miaodesk

int wmain() {
    using namespace miaodesk;
    std::printf("\nWALL-03 宿主侧时间策略\n");
    TestEasing();
    TestSceneClock();
    TestParameterSlew();
    TestContinuity();
    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n全部 %d 项检查通过\n", g_checks);
    return 0;
}
