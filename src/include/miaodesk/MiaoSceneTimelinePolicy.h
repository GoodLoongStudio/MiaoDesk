#pragma once

// WALL-03 动画、状态与有界行为:宿主侧的时间策略。
//
// 为什么要有这个头文件:MiaoSceneRuntime 把"某一时刻场景该是什么值"算对了,但它回答不了
// 三件宿主必须回答的问题,而这三件事此前一件都没有实现:
//
//   ① 暂停恢复 —— AdvanceTimeline(timeSeconds) 收的是宿主给的时间。真实宿主给的是
//      GetTickCount64()/1000(壁纸被全屏游戏盖住、组件所在的桌面被切走、用户锁屏),
//      于是被挡住的那 30 秒会连着算进动画里。昼夜壁纸恢复后天空直接跳到半夜,
//      而用户什么都没做。规划的验收项"暂停恢复"指的就是这个,不是"暂停后再点继续"。
//
//   ② 平滑参数过渡 —— SetParameter 是瞬移的。用户在配置面把强度从 0 拖到 1,桌面上
//      那一帧就跳过去。规划里"平滑参数过渡"是一项能力,此前为零。
//
//   ③ 循环接缝与跳变 —— Loop 轨道的第一帧和最后一帧值不同时,每过一圈就硬跳一次。
//      三个内置壁纸的 8 条轨道端点都相同(所以现在看不出问题),而 Validate 只查
//      "时间严格递增",查不出端点不一致,也查不出"两个关键帧相距 1e-9 秒、值差 0.8"
//      这种一帧之内跨过大半行程的写法。作者没有任何地方能看到这件事。
//
// 这三件事共同点:都是纯逻辑,都不碰盘、不 import Windows 头。于是"暂停不跳变"、
// "过渡连续"、"循环无缝"这些性质在本机就能真验,不需要一台装了 Windows 的机器 ——
// 而它们恰好是每轮都该跑的那批。
//
// 边界:这里不画一个像素,不碰一个设备。它给宿主提供判断与数值,真正的暂停由宿主调
// Pause/Resume,真正的过渡由宿主在一段时间里反复 SetParameter。把动作留给宿主,
// 是为了这个模块不被任何渲染后端绑住。
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"

namespace miaodesk::content {

// ---------------------------------------------------------------------------
// 一、缓动曲线
//
// 这一段从 MiaoSceneRuntime.cpp 的匿名命名空间里提出来,是为了"平滑过渡"和"时间轴"
// 用**同一条**曲线。原先 ApplyEasing 是文件局部符号,时间轴调它、宿主过渡调不到,
// 于是任何一处想自己写一条 ease-out 就会出现两条曲线 —— 而两条曲线的差别在
// "拖滑块时壁纸跟手不跟手"上是看得出来的。
// ---------------------------------------------------------------------------

// 归一化输入,输出仍是 0..1。非有限输入按 0 处理(NaN 不应该变成一帧乱跳)。
//
// 四条曲线都为纯函数、无状态、不访问宿主,所以"有界"是可证的:任何输入都落在 [0,1]。
double ApplyAnimationEasing(AnimationEasing easing, double normalized) noexcept;

// ---------------------------------------------------------------------------
// 二、暂停恢复:场景时钟
// ---------------------------------------------------------------------------

// 场景时间只由"正在跑的那段时间"累加而成,与墙钟无关。
//
// 宿主被挡住、隐藏、锁屏、或者用户主动暂停时调 Pause();恢复时调 Resume()。
// 关键性质只有一条:**暂停任意时长,场景时间一个普朗克时间都不多走**。
//
// 这不是性能优化(暂停期间不渲染),是正确性:一个 86400 秒的昼夜循环被挡 10 分钟,
// 恢复后必须还是同一个时刻的天空,而不是直接跳到 10 分钟之后。
class SceneClock {
public:
    // 从 sceneTimeSeconds 开始跑。默认 0。
    void Start(double sceneTimeSeconds = 0.0) noexcept;

    // 冻结场景时间。已暂停时再调是空操作(不推进 startWall)。
    void Pause() noexcept;

    // 解冻并继续。已运行时再调是空操作。
    void Resume() noexcept;

    // 按墙钟增量推进。暂停时返回当前场景时间而不推进 —— 宿主被挡住的那段时间
    // 就是从这里被丢掉的,而"丢掉"正是这里的意图。
    double Advance(double wallDeltaSeconds) noexcept;

    // 直接把场景时间设到一个值(宿主要对齐外部时钟时用,例如音频播放头)。
    // 只在运行时生效,否则会绕过暂停。
    void Seek(double sceneTimeSeconds) noexcept;

    double Now() const noexcept { return sceneTime_; }
    bool Running() const noexcept { return running_; }
    // 累计被丢掉的墙钟时长,给诊断用:用户问"怎么感觉卡了一下"时这是唯一的答案。
    double SkippedWallSeconds() const noexcept { return skippedWall_; }

    void Reset() noexcept;

private:
    double sceneTime_{};
    double skippedWall_{};
    bool running_{};
};

// ---------------------------------------------------------------------------
// 三、平滑参数过渡
// ---------------------------------------------------------------------------

// 一条正在进行的参数过渡。from/to 是宿主给的旧值与新值,曲线与时间轴共用。
//
// 为什么不是"设个目标让它自己追":那样每次 Sample 都要知道目标变了没有,
// 而宿主改目标的那一帧必须能从**新目标与当前值**重新起跳,否则会出现
// "拖到一半放松手,滑块先弹回起点再走"—— 那种回弹比瞬移更难看。
// 所以 Begin(from, to, ...) 每次重新起跳,当前值由宿主在上一次 Sample 时存下来。
struct ParameterSlewRequest {
    // 过渡时长。<= 0 表示不要过渡(等价于瞬移,保留给明确不想平滑的场合)。
    double durationSeconds{0.25};
    AnimationEasing easing{AnimationEasing::EaseOut};
    // 旧值。缺省为 0,于是"从静止开始"不需要宿主填。
    double from{};
    // 新值,即宿主刚设进去的那个值。
    double to{};
};

// 过渡求值结果。
struct ParameterSlewSample {
    // 此刻应该喂给 SetParameter 的值。
    double value{};
    // 过渡是否已经结束。true 之后宿主可以停止为这条参数排帧 ——
    // 这正是"有界"的实用含义:过渡不会永远占着一帧。
    bool settled{};
};

// 单条过渡求值。
//
// elapsed 是过渡开始后经过的**场景**时间,不是墙钟:宿主暂停时过渡也暂停,
// 否则一次锁屏会让所有进行中的过渡一起跳到终点。
ParameterSlewSample SampleParameterSlew(const ParameterSlewRequest& request, double elapsedSeconds) noexcept;

// 一组同时进行的参数过渡,按参数 id 各一条。
//
// 同一个参数在过渡途中又被设新值时会**从头开始一条新过渡**(而不是排队),
// 因为用户连续拖动滑块时,排队的过渡会让滑块永远追不上手。
class ParameterSlewSet {
public:
    // 记录/重启一条过渡。返回此刻的值(与 Begin 后立即 Sample(elapsed=0) 一致)。
    double Begin(const std::wstring& parameterId, const ParameterSlewRequest& request) noexcept;

    // 以场景时间增量推进所有过渡,把未结束的逐个求值写进 out。
    // 已结束的过渡保持在终值、不再出现在 out 里 —— 宿主据此可以停止排帧。
    // 返回 false 表示没有任何过渡还在进行。
    //
    // 参数是增量而不是"此刻的绝对时间":多条过渡的开始时刻各不相同,共用一个绝对
    // 时间会让后开始的那条一上来就被判定为结束。
    bool Advance(double sceneDeltaSeconds, std::vector<std::pair<std::wstring, double>>* out);

    // 还有多少条过渡没结束(宿主决定排帧间隔用)。
    std::size_t ActiveCount() const noexcept;

    bool Active(const std::wstring& parameterId) const noexcept;
    double ValueOf(const std::wstring& parameterId) const noexcept;
    void Reset() noexcept;

private:
    struct Entry {
        ParameterSlewRequest request;
        double elapsed{};
        bool settled{};
    };
    std::unordered_map<std::wstring, Entry> entries_;
};

// ---------------------------------------------------------------------------
// 四、循环接缝与跳变:一条轨道"看得见地连续"吗
// ---------------------------------------------------------------------------

enum AnimationContinuityIssue : std::uint32_t {
    kAnimationContinuous = 0,
    // Loop 轨道首尾关键帧值不同:每过一圈硬跳一次。昼夜壁纸的接缝正是这一处。
    kAnimationLoopSeamMismatch = 1u << 0,
    // 某一段在一帧之内跨过轨道自身行程的可辨部分:插值根本采不到样,看起来是瞬移。
    kAnimationFrameJump = 1u << 1,
    // Loop 轨道的第一个关键帧不在 t=0,或最后一个不在 duration:每一圈先/后有一段时间
    // 死死停在端点值上。不是跳变,但几乎总是作者以为自己在整段里做动画。
    kAnimationLoopHoldAtEdge = 1u << 2,
    // 目标属性上还有别的写者。**报告,不是拒绝** —— 两种写者的可观测性不同:
    //   · 另一条动画:先声明的那条从来不可观测,Validate 直接拒(见 ContestedAnimationTargets)
    //   · 一条 binding:它在 Initialize 提供起始值,是活的;动画开跑之后被盖住
    // 于是这一位只说明"这里有优先级",由宿主与 Skill 讲清楚,不由验证器禁止。
    kAnimationPropertyContested = 1u << 3,
};

// 两个数值型属性值之间的"距离"。非数值型(作者的 Validate 已经拒了)返回 0。
double AnimationValueDistance(const PropertyValue& left, const PropertyValue& right) noexcept;

// 一条轨道自身值行程的大小(任意两个关键帧值之间的最大距离)。
// 恒定轨道的行程是 0 —— 此时"跳变比例"没有意义,调用方必须处理这一情形,
// 不能拿 0 做分母。
double AnimationTrackValueRange(const AnimationTrackDefinition& track) noexcept;

// 循环接缝:Loop 轨道回到起点时跳过多远,以**轨道自身行程的倍数**计。
// 0 表示无缝(也包含 PingPong/Once:它们结构上不可能在这处接缝)。行程为 0 时返回 0。
double AnimationLoopSeamFraction(const AnimationTrackDefinition& track) noexcept;

// 一帧之内最多跨过轨道自身行程的多大比例。
//
// 在 fps 下逐段算:段内值距离 / 行程,再乘上一帧占该段的比例。取全轨道最大值
// (Loop 还要把接缝那一步算进去 —— 那一步恰好是最容易超限的地方)。
// 行程为 0 时返回 0(恒定轨道不存在跳变)。
double AnimationFrameStepFraction(const AnimationTrackDefinition& track, double fps) noexcept;

// 一帧之内跨过自身行程的这个比例,就应当视为"看起来是瞬移"。
//
// 取 0.25 而不是某个更精确的数:渲染是以帧为单位的,四分之一行程在一帧内走完,
// 任何帧率下都不是插值而是替换。这个门槛同时留给未来调优 —— 它是一个判断,
// 不是一个物理常数,改它必须连理由一起写进变更记录。
inline constexpr double kAnimationFrameJumpThreshold = 0.25;

// Loop 首尾值相差超过自身行程的这个比例,即判为接缝不连续。
//
// 用比例而不是绝对误差,是因为行程可能是 3 个场景单位,也可能是 3000 个:
// 固定绝对阈值在大行程上会把真接缝放过,在小行程上会把无缝判成有缝。
// 1e-4 留给 JSON 往返与 double 累加的余地 —— 实测三个内置壁纸的端点是逐位相同的。
inline constexpr double kAnimationLoopSeamThreshold = 1e-4;

// Loop 轨道的端点被允许偏离 0 / duration 多久。超过即报 kAnimationLoopHoldAtEdge。
inline constexpr double kAnimationLoopEdgeToleranceSeconds = 1e-3;

// 审查一条轨道,返回 kAnimation* 的按位或。0 表示这条轨道看起来是连续的。
std::uint32_t AuditAnimationContinuity(
    const AnimationTrackDefinition& track,
    const std::vector<PropertyBindingDefinition>* bindings,
    double fps) noexcept;

// 点名"被另一个写者盖住"的轨道与绑定。
//
// 为什么单独给这个函数:Validate 拒绝两条动画写同一属性是硬的,但拒绝之前要说清楚是谁
// 多余 —— 错误信息里只有属性名的话,作者得自己数 16 条轨道才知道该删哪条。
// 而 binding 那一侧**不是**拒绝:binding 只在 Initialize 跑一次,所以它在动画开跑之前
// 是可观测的(属性起始值来自参数),"起始值取参数、之后交给动画"是一个自洽的模型。
// 所以这里分两层返回,由调用方决定是拒绝还是只报告。
//
//   两条动画写同一属性 → 点名先声明的那条(它从来不可观测)
//   binding 与动画写同一属性 → 点名那条 binding(动画开跑后它被盖住)
std::vector<std::wstring> ContestedAnimationTargets(
    const std::vector<AnimationTrackDefinition>& animations,
    const std::vector<PropertyBindingDefinition>& bindings) noexcept;

// 把 kAnimation* 位翻译成给人看的一行字(给宿主诊断与测试输出用)。
std::wstring DescribeAnimationContinuity(std::uint32_t issues) noexcept;

} // namespace miaodesk::content
