// WALL-03:所有随产品发行、以及作者能照抄的包,其动画轨道必须"看得见地连续"。
//
// 为什么要单独一道门:MiaoSceneTimelinePolicyTest 钉的是**判断**本身对不对,
// 而这里钉的是**真的内容**有没有问题。两者不能互相代替 —— 判断对而内容坏,
// 和内容好而判断坏,是两种失败。
//
// 具体防的回归:某天有人改一个内置壁纸,把 Loop 轨道的最后一个关键帧值动了一下
// (比如调了一次亮度),端点从此不再相同,于是每过一圈天空硬跳一次。那件事不会有任何
// 测试红:Validate 只查时间递增,MiaoSceneRuntimeTest 不覆盖动画。而它是用户每 8 秒
// 就看一次的东西。
//
// 本轮实测:三个内置壁纸 24 条 Loop 轨道 + 一个示例 1 条 PingPong 轨道,端点全部逐位
// 相同;其中一条有一个 1/240 秒的淡变(见下面 RegisteredSubFrameTracks 的说明)。
// 这道门把当前状态固定下来,并且**变化就会红**。
//
// 走的是真实加载链(MiaoContentPackage::Load → 反序列化 → Validate),
// 与 ExamplePackagesLoad 同一套做法:不读 JSON 猜字段,让加载器告诉我们它看到什么。
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSceneSerializer.h"
#include "miaodesk/MiaoSceneTimelinePolicy.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace miaodesk::content;
namespace fs = std::filesystem;

static int failures = 0;
static int checks = 0;
static std::size_t g_tracks = 0;

static void Check(bool ok, const char* what) {
    ++checks;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// 从仓库根向上找,答案与进程当前目录无关(源码里也不许依赖 cwd,
// 见 verify-path-layout-contract.ps1)。
static fs::path FindRepoRoot() {
    fs::path here = __FILE__;
    for (std::size_t depth = 0; depth < 8 && !here.empty(); ++depth) {
        const fs::path candidate =
            here.parent_path().parent_path().parent_path() / "assets" / "wallpapers";
        std::error_code ec;
        if (fs::exists(candidate, ec)) return here.parent_path().parent_path().parent_path();
        here = here.parent_path();
    }
    return {};
}

// 已登记的一帧内完成位移的轨道。登记不是"放过",是把已知的悬着的问题写在代码里,
// 让下一轮能看到它;静悄悄地放过比报错更糟。
//
// 现在只有一条:MiaoCloud 的 animation://miao-cloud/blink-blink。
// 它的"淡入"是 2.829204 → 2.833370666666667 秒,差 0.004166666666667 = 1/240 秒 ——
// 恰好是产品支持的最高帧率下的一帧。于是 1~240fps 的任何一档,插值都一次采不到样:
// 每一帧上 opacity 只能是 0 或 1,作者写的淡变是惰性的。
//
// 为什么**不**在这一轮改它:改法是把淡变拉长到看得见的时长(比如 60ms),那是改美术。
// 本机不是 Windows,一个眨眼的淡变拉长之后好看不好看我给不出证据,而"为了过一道门
// 去改一个我看不见效果的东西"正是这个规划禁止的那类动作。所以登记它、报出来,
// 等真机上看过再定。
//
// 契约是紧的:多一条、少一条都会红。多一条说明有人新写了看不见的位移,
// 少一条说明登记的问题已经不在了而这张表没跟着更新。
static std::set<std::wstring> RegisteredSubFrameTracks() {
    return {L"animation://miao-cloud/blink-blink"};
}

int wmain() {
    const fs::path root = FindRepoRoot();
    if (root.empty()) {
        std::printf("\n[FAIL] 找不到含 assets/wallpapers 的仓库根(从 %s 向上找了 8 层)\n",
                    __FILE__);
        return 1;
    }

    // 只认 .mdwall / .mdwidget 目录:assets/wallpapers 下还有一个 assets/ 目录,
    // 它不是包(ShippedPackagesValidate 第一版就是那么踩的)。
    auto isPackage = [](const fs::path& dir) {
        const std::string ext = dir.extension().string();
        return ext == ".mdwall" || ext == ".mdwidget";
    };
    struct Where {
        const char* dir;
        const char* label;
    };
    std::set<std::wstring> subFrameTracks;

    // 一条轨道的"要不要让这一轮失败"的判定。抽成函数而不是写在循环里,是为了下面
    // 的自检能走**同一条**判定 —— 否则把循环里的判断删掉,自检照样绿,而那道门
    // 从此对所有内容都说"没问题"。这正是"几乎通过"的测试:一个永远不失败的门,
    // 与一道好门在通过时长得一模一样。
    //
    // 接缝断裂判失败:动画走完整个行程,然后在一帧之内把它全部撤回。这与"闪一下"
    // 不同 —— 闪一下是有意的,而行程被撤回永远不好看。
    // 一帧内大位移只登记不失败:见 RegisteredSubFrameTracks 的说明。
    auto fatal = [](std::uint32_t issues) {
        return (issues & kAnimationLoopSeamMismatch) != 0;
    };
    auto hold = [](std::uint32_t issues) { return (issues & kAnimationFrameJump) != 0; };

    // 反空洞自检:审查函数与上面的判定,必须都还抓得住坏轨道。第一版只喂了一个合成
    // 坏轨道给审查函数,然后**在循环里另写一遍判断** —— 于是把循环里的判断删掉,
    // 自检仍然绿,而门从此恒过。现在自检走的就是判定本身。
    {
        AnimationTrackDefinition broken;
        broken.id = L"animation://self-check/must-be-flagged";
        broken.target = PropertyAddress{L"component://root/transform", L"opacity"};
        broken.loopMode = AnimationLoopMode::Loop;
        broken.durationSeconds = 2.0;
        broken.keyframes = {AnimationKeyframeDefinition{0.0, 0.2, AnimationEasing::Linear},
                            AnimationKeyframeDefinition{1.0, 0.9, AnimationEasing::Linear},
                            AnimationKeyframeDefinition{2.0, 1.0, AnimationEasing::Linear}};
        const auto issues = AuditAnimationContinuity(broken, nullptr, 60.0);
        Check(fatal(issues), "自检:端点不同的 Loop 轨道必须被判为致命(否则每一轮都空洞地通过)");
        Check(hold(issues), "自检:同一坏轨道还必须判为'一帧内大位移'");
        Check(!fatal(kAnimationContinuous) && !hold(kAnimationContinuous),
              "自检:没问题的轨道不得被判为致命");
        Check(!fatal(kAnimationLoopHoldAtEdge), "自检:端点悬停不致命(它不跳变)");
        Check(!fatal(kAnimationPropertyContested), "自检:并发写不致命(binding 那侧是合法写法)");
        Check(fatal(kAnimationLoopSeamMismatch), "自检:单把接缝位移为致命");
        AnimationTrackDefinition pingPong = broken;
        pingPong.loopMode = AnimationLoopMode::PingPong;
        const auto pingIssues = AuditAnimationContinuity(pingPong, nullptr, 60.0);
        Check(!fatal(pingIssues), "自检:PingPong 结构上折返,不得判为接缝断裂");
    }

    for (const auto where : {Where{"assets/wallpapers", "官方壁纸"},
                             Where{"assets/widgets", "官方组件"},
                             Where{"examples/content", "示例包"}}) {
        std::error_code ec;
        const fs::path base = root / where.dir;
        if (!fs::exists(base, ec)) continue;
        for (fs::directory_iterator it(base, ec), end; it != end; it.increment(ec)) {
            if (ec || !it->is_directory(ec) || ec) continue;
            if (!isPackage(it->path())) continue;
            const auto label = std::string(where.label) + " " + it->path().filename().string();

            std::wstring error;
            LoadedMiaoContentPackage package;
            if (!MiaoContentPackage::Load(it->path(), &package, &error)) {
                std::printf("  [FAIL] %s 加载失败:%ls\n", label.c_str(), error.c_str());
                ++failures;
                continue;
            }
            SceneRuntimeDefinition runtime;
            if (!MiaoSceneSerializer::DeserializePackage(package, &runtime, &error)) {
                std::printf("  [FAIL] %s 反序列化失败:%ls\n", label.c_str(), error.c_str());
                ++failures;
                continue;
            }
            if (!MiaoSceneRuntimeModel::Validate(runtime, &error)) {
                std::printf("  [FAIL] %s 场景校验失败:%ls\n", label.c_str(), error.c_str());
                ++failures;
                continue;
            }

            ++checks;
            std::printf("  [PASS] %s 走通 load→反序列化→Validate\n", label.c_str());
            g_tracks += runtime.animations.size();
            if (runtime.animations.empty()) {
                std::printf("         (没有动画轨道)\n");
                continue;
            }

            // 并发写:报告,不失败。两条动画写同一属性 Validate 已经拒了(走不到这里),
            // 所以这里只会剩"binding 与动画写同一属性"—— 那是合法写法(binding 在
            // Initialize 提供起始值)。点出来是为了让"谁被盖住"在报告里看得见,
            // 而不是为了让这一条变成一条静默通过的空断言。
            const auto contested = ContestedAnimationTargets(runtime.animations, runtime.bindings);
            if (contested.empty()) {
                Check(true, "没有 binding 被动画盖住");
            } else {
                ++checks;
                std::printf("  [INFO] %zu 条 binding 与动画写同一属性(合法:它们提供起始值)\n",
                            contested.size());
                for (const auto& id : contested) std::printf("         %ls\n", id.c_str());
            }

            bool seamBroken = false;
            for (const auto& animation : runtime.animations) {
                // 60fps 是产品默认动画帧率(kDefaultAnimationFps),也是用户最常见的那一档。
                const auto issues = AuditAnimationContinuity(animation, &runtime.bindings, 60.0);
                if (fatal(issues)) {
                    std::printf("  [FAIL] %ls %ls\n", animation.id.c_str(),
                                DescribeAnimationContinuity(issues).c_str());
                    ++failures;
                    seamBroken = true;
                    continue;
                }
                if (hold(issues)) {
                    subFrameTracks.insert(animation.id);
                    std::printf("  [WARN] %ls %ls\n", animation.id.c_str(),
                                DescribeAnimationContinuity(issues).c_str());
                } else {
                    std::printf("  [PASS] %ls 连续\n", animation.id.c_str());
                    ++checks;
                }
                if (issues & kAnimationLoopHoldAtEdge) {
                    std::printf("         (另有:%ls)\n",
                                DescribeAnimationContinuity(kAnimationLoopHoldAtEdge).c_str());
                }
            }
            if (!seamBroken) {
                ++checks;
                std::printf("  [PASS] %s 全部 %zu 条轨道没有循环接缝断裂\n", label.c_str(),
                            runtime.animations.size());
            }
        }
    }

    // 登记表的契约:与实际发现的一致。不一致就是红,并说清楚该怎么办。
    const auto registered = RegisteredSubFrameTracks();
    if (subFrameTracks != registered) {
        ++failures;
        std::printf("  [FAIL] 一帧内完成位移的轨道集合变了(实际 %zu 条,登记 %zu 条)\n",
                    subFrameTracks.size(), registered.size());
        for (const auto& id : subFrameTracks)
            if (registered.count(id) == 0)
                std::printf("         新出现(要么把位移拉长到看得见,要么连理由一起登记):%ls\n",
                            id.c_str());
        for (const auto& id : registered)
            if (subFrameTracks.count(id) == 0)
                std::printf("         已不存在(从登记表里删掉):%ls\n", id.c_str());
    } else if (!registered.empty()) {
        ++checks;
        std::printf("  [PASS] 一帧内完成位移的轨道与登记表一致(%zu 条,见源码里的说明)\n",
                    registered.size());
    }

    if (failures != 0) {
        std::printf("\n失败 %d / %d(共审查 %zu 条动画轨道)\n", failures, checks, g_tracks);
        return 1;
    }
    std::printf("\n发行内容动画连续性:全部 %d 项通过(共 %zu 条动画轨道)\n", checks, g_tracks);
    return 0;
}
