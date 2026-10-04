// AI-05「当前任务不静默换 Provider」那道闸门的回归。
//
// 这段判定原本散在**五处**、四种写法、两种相反结论,而它们管的是同一个共享 Pi
// 运行时,并且都住在本机跑不到的文件里(ConversationPanelImpl.inc 与
// ContentCreatorDialog.cpp)。完整故事写在 MiaoProfileSwitchGuard.h 的头注释里 ——
// 一句话:`&& !busy` 那个豁免让"这个窗口自己的轮次正在跑"成为重建下拉的理由,
// 而重建通向 ReloadConfig 的 Stop 与 conversation_ 清空。
//
// 这里钉住:首填与运行期刷新的分界、任一忙位为真就不动(不只是 piBusy ——
// busy 已置真而 Pi 还不忙的那段间隙真实存在)、以及 Keep 只在 preserveSelection
// 为真时出现(调用点靠这条省掉一层永远为真的判断)。
#include "miaodesk/MiaoProfileSwitchGuard.h"

#include <cstdio>
#include <string>

namespace miaodesk {
namespace profile_switch {
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

// 反空洞自检。两个方向各喂一个,两个方向都实测过会变红:
//   恒 Keep   → 窗口永远填不上 profile 列表:用户打开 AI 窗口看到"未配置 API",
//               而其实配了;
//   恒 Rebuild→ 轮次跑到一半被重建,Stop 掉、conversation_ 被清空。
bool VerdictStillMoves() {
    return DecideProfileSelectorAction(false, true, true) == ProfileSelectorAction::Rebuild &&
           DecideProfileSelectorAction(true, true, false) == ProfileSelectorAction::Keep;
}

} // namespace
} // namespace profile_switch
} // namespace miaodesk

int wmain() {
    using namespace miaodesk::profile_switch;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:闸门判定不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:首填照重建、有轮次在飞就留住\n");
    ++g_checks;

    // ---- 1. 首次填充:随便重建 ----
    // 窗口刚打开,这个窗口还没有轮次,用户也没做过选择。
    {
        Check(DecideProfileSelectorAction(false, false, false) == ProfileSelectorAction::Rebuild,
              "刚打开、什么都没有 → 重建");
        // 首填时共享运行时**已经有**别的窗口的轮次在飞,也照重建:
        // 这个窗口的下拉还没给用户看过,重建它不妨碍谁。
        Check(DecideProfileSelectorAction(false, true, false) == ProfileSelectorAction::Rebuild,
              "首填时别的窗口有轮次在飞 → 仍重建(这个窗口的下拉还没人看过)");
    }

    // ---- 2. 运行期刷新:有轮次在飞就别动 ----
    {
        Check(DecideProfileSelectorAction(true, true, false) == ProfileSelectorAction::Keep,
              "别的窗口占着共享轮次 → 留住");
        Check(DecideProfileSelectorAction(true, true, true) == ProfileSelectorAction::Keep,
              "这个窗口自己的轮次在飞 → 也留住(重建会 Stop() 掉它、清掉上下文)");
    }

    // ---- 3. 运行期刷新且没有轮次:照重建 ----
    {
        Check(DecideProfileSelectorAction(true, false, false) == ProfileSelectorAction::Rebuild,
              "运行期刷新、没有轮次 → 重建(外部改了配置要能生效)");
        // 这条是自我更正后新加的:这个窗口 busy、Pi 反而不忙,busy 仍然算数。
        // (第一版这里写的是"→ 重建",依据是"windowBusy 必然带着 piBusy",而那句
        // 证不了:SetBusyVisual(state,true) 在 AskAsync 之前,AskAsync 被共享运行时
        // 拒掉时 busy 也不收回。)
        Check(DecideProfileSelectorAction(true, false, true) == ProfileSelectorAction::Keep,
              "运行期刷新、这个窗口 busy 但 Pi 不忙 → 仍留住(busy 单独算一项)");
    }

    // ---- 4. 穷举:八种组合全部有确定结论 ----
    // 这一条防"某种组合永远走不到":一个不可达的分支与没有它长得一样。
    {
        int keep = 0, rebuild = 0;
        for (int ps = 0; ps <= 1; ++ps) {
            for (int pb = 0; pb <= 1; ++pb) {
                for (int wb = 0; wb <= 1; ++wb) {
                    const auto a = DecideProfileSelectorAction(ps == 1, pb == 1, wb == 1);
                    Check(a == ProfileSelectorAction::Keep || a == ProfileSelectorAction::Rebuild,
                          "组合必须有确定结论");
                    if (a == ProfileSelectorAction::Keep) ++keep; else ++rebuild;
                }
            }
        }
        Check(keep > 0 && rebuild > 0, "两种结论都可达(不是永远一种)");
        // 调用点靠这条不变式省掉一层判断:两个调用方在 Keep 分支里都**没有**再套
        // `if (preserveSelection)`,因为那条永远为真。谁把这条断言删了,那个不变式
        // 就没有人来守,而死判断会悄悄长回来。
        for (int ps = 0; ps <= 1; ++ps) {
            for (int pb = 0; pb <= 1; ++pb) {
                for (int wb = 0; wb <= 1; ++wb) {
                    if (DecideProfileSelectorAction(ps == 1, pb == 1, wb == 1) ==
                        ProfileSelectorAction::Keep) {
                        Check(ps == 1,
                              "Keep 只在 preserveSelection 为真时出现"
                              "(调用点因此不需要内层 preserveSelection 判断)");
                    }
                }
            }
        }
        // 语义陈述:Keep 当且仅当 运行期刷新 && 任一忙位为真。
        for (int ps = 0; ps <= 1; ++ps) {
            for (int pb = 0; pb <= 1; ++pb) {
                for (int wb = 0; wb <= 1; ++wb) {
                    const auto a = DecideProfileSelectorAction(ps == 1, pb == 1, wb == 1);
                    // 任一忙位为真就不动。不是"piBusy 就够":busy 已置真而 Pi 还
                    // 不忙的那段间隙真实存在(SetBusyVisual 在 AskAsync 之前),
                    // 而重建落进那段间隙里就是 Stop() + conversation_.clear()。
                    const bool expectKeep = (ps == 1 && (pb == 1 || wb == 1));
                    Check((a == ProfileSelectorAction::Keep) == expectKeep,
                          "Keep 当且仅当 运行期刷新 && 有轮次在飞(ps=" + std::to_string(ps) +
                              ",pb=" + std::to_string(pb) + ",wb=" + std::to_string(wb) + ")");
                }
            }
        }
    }

    // ---- 5. Keep 时必须留下那句话 ----
    // 用户点 API 位置/拉下拉发现它不动,得知道为什么。这句话原来在三处各写一遍,
    // 还有两套措辞("当前有 AI 任务…" 与 "另一个 AI 窗口正在执行任务…"),
    // 而后者带一个它证明不了的主张 —— 那只在排除了本窗口 busy 之后才成立。
    // 现在只有一个出处。
    {
        const std::wstring text = ExplainProfileSelectorAction(ProfileSelectorAction::Keep);
        Check(!text.empty(), "Keep 有话说");
        // 用宽字面量比:文案是给最终用户看的中文,不能在这里被编码层悄悄改掉。
        Check(text.find(L"任务") != std::wstring::npos, "Keep 的话说明有任务在执行");
        Check(text.find(L"切换 API") != std::wstring::npos, "Keep 的话说出用户想做的事");
        // Rebuild 必须是空串,不能是 nullptr —— 调用方有的是直接 SetWindowTextW,
        // 收不了空指针;回空串让"不说"和"说"走同一条路。
        const std::wstring rebuildText =
            ExplainProfileSelectorAction(ProfileSelectorAction::Rebuild);
        Check(rebuildText.empty(), "Rebuild 回空串(不是 nullptr:调用方直接 SetWindowTextW)");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\nprofile 切换闸门:全部 %d 项通过\n", g_checks);
    return 0;
}
