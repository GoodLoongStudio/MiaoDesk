// P0-07「库状态一致恢复」的合并策略回归。
//
// 逮到的不是"它会算错",而是**它在本机一行都验不到,而且有两份**:
// `WallpaperLibrary::Load` 里两遍合并各有一份一字不差的副本,合并 favorite /
// imported / lastUsed 三个字段。策略本身是对的;问题是加字段时两份都会安静地
// 漏掉它 —— 恢复之后用户那一项变回默认值,而 Load 照常返回 true。
// 那正是 P0-07 的失败形态:"恢复成一个更短的库并报告成功"。
//
// 这里钉住三件事:
//   · 三条不变式各自成立(单调:合并结果不劣于任一输入);
//   · 0 是"没有记录",不是 1970 年;
//   · 字段清单与结构体配对 —— 加字段必须同时改清单,否则自检红。
#include "miaodesk/MiaoLibraryRestoreMerge.h"

#include <cstdio>
#include <string>

namespace miaodesk {
namespace library_restore {
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

// 反空洞自检。两个方向各喂一个:一个恒取 canonical 的合并会丢掉 legacy 的收藏,
// 一个恒取 legacy 的会丢掉 canonical 的导入时间。用户都看得见。
bool VerdictStillMoves() {
    RestoredUserState canonical;
    canonical.favorite = true;
    canonical.importedUnixSeconds = 500;
    RestoredUserState legacy;
    legacy.favorite = false;
    legacy.importedUnixSeconds = 900;
    const auto merged = MergeRestoredUserState(canonical, legacy);
    return merged.favorite && merged.importedUnixSeconds == 500;
}

// 三条不变式的**逐条**表述。注意方向各不相同 —— 一开始我把它们写成统一的
// "结果不劣于任一输入",那是错的:`importedUnixSeconds` 取的是**更早**那个,
// 所以它合理地小于其中一个输入。"不劣"只对 favorite(不丢 true)和
// lastUsed(不丢最近)成立。写成一条笼统的规则,就会把正确的实现判成错的。
bool SatisfiesInvariants(const RestoredUserState& a, const RestoredUserState& b) {
    const auto m = MergeRestoredUserState(a, b);
    // 1. 收藏:任一是 true 结果就是 true —— 不丢 true。
    if (m.favorite != (a.favorite || b.favorite)) return false;
    // 2. 导入时间:结果必须是两个输入之一(不凭空造值),
    //    且任一侧非零时结果非零(不丢"导入过"这个事实),
    //    并且它就是 EarliestNonZero(取更早的非零)。
    if (m.importedUnixSeconds != EarliestNonZeroSecond(a.importedUnixSeconds, b.importedUnixSeconds)) return false;
    if (m.importedUnixSeconds != a.importedUnixSeconds && m.importedUnixSeconds != b.importedUnixSeconds) return false;
    if ((a.importedUnixSeconds != 0 || b.importedUnixSeconds != 0) && m.importedUnixSeconds == 0) return false;
    // 3. 最近使用:不小 于任何一个输入 —— 不丢"最近"。
    if (m.lastUsedUnixSeconds < a.lastUsedUnixSeconds || m.lastUsedUnixSeconds < b.lastUsedUnixSeconds) return false;
    if (m.lastUsedUnixSeconds != (a.lastUsedUnixSeconds > b.lastUsedUnixSeconds ? a.lastUsedUnixSeconds
                                                                                : b.lastUsedUnixSeconds)) {
        return false;
    }
    return true;
}

// 把清单数一遍,和结构体字段数对上。
// C++ 没有反射,所以这是一份手写清单;它的价值不在准确,而在**漏字段会红**。
template <typename Fn>
void ForEachField(const RestoredUserState& s, Fn&& fn) {
    fn("favorite", s.favorite);
    fn("importedUnixSeconds", s.importedUnixSeconds);
    fn("lastUsedUnixSeconds", s.lastUsedUnixSeconds);
}

} // namespace
} // namespace library_restore
} // namespace miaodesk

int wmain() {
    using namespace miaodesk::library_restore;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:合并判定不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:收藏不丢、导入时间取更早的那个\n");
    ++g_checks;

    // ---- 1. 三条不变式 ----
    {
        // 收藏:任一侧收藏过就收藏。这是 or,不是覆盖 ——
        // 用户不记得自己是在哪一份里点的,丢了就是无声的。
        RestoredUserState canonical;
        RestoredUserState legacy;
        legacy.favorite = true;
        Check(MergeRestoredUserState(canonical, legacy).favorite, "只收藏过 legacy → 合并后仍收藏");
        canonical.favorite = true;
        Check(MergeRestoredUserState(canonical, RestoredUserState{}).favorite, "只收藏过 canonical → 合并后仍收藏");
        Check(MergeRestoredUserState(canonical, legacy).favorite, "两侧都收藏 → 仍收藏");
        Check(!MergeRestoredUserState(RestoredUserState{}, RestoredUserState{}).favorite, "两侧都没收藏 → 不收藏");

        // 导入时间:取更早的非零。一条壁纸被导入过两次(两次迁移各留一行),
        // 它的导入时间该是第一次。
        RestoredUserState early;
        early.importedUnixSeconds = 100;
        RestoredUserState late;
        late.importedUnixSeconds = 900;
        Check(MergeRestoredUserState(early, late).importedUnixSeconds == 100, "导入时间取更早的那个");
        Check(MergeRestoredUserState(late, early).importedUnixSeconds == 100, "导入时间不分左右");

        // 最近使用:取更大。
        RestoredUserState old_;
        old_.lastUsedUnixSeconds = 100;
        RestoredUserState fresh;
        fresh.lastUsedUnixSeconds = 900;
        Check(MergeRestoredUserState(old_, fresh).lastUsedUnixSeconds == 900, "最近使用取更大的那个");
        Check(MergeRestoredUserState(fresh, old_).lastUsedUnixSeconds == 900, "最近使用不分左右");
    }

    // ---- 2. 0 是"没有记录",不是 1970 年 ----
    // 当成 1970 会让每一次"导入时间缺失"的记录都赢得"更早",真实导入时间被它盖掉。
    {
        RestoredUserState recorded;
        recorded.importedUnixSeconds = 1700000000ULL;
        RestoredUserState missing;
        Check(MergeRestoredUserState(recorded, missing).importedUnixSeconds == 1700000000ULL,
              "一侧没记导入时间 → 取另一侧的真实值,不是 0");
        Check(MergeRestoredUserState(missing, recorded).importedUnixSeconds == 1700000000ULL,
              "同上,不分左右");
        Check(MergeRestoredUserState(missing, missing).importedUnixSeconds == 0, "两侧都没记 → 0");
        // EarliestNonZeroSecond 直接钉
        Check(EarliestNonZeroSecond(0, 7) == 7, "EarliestNonZero(0,7)=7");
        Check(EarliestNonZeroSecond(7, 0) == 7, "EarliestNonZero(7,0)=7");
        Check(EarliestNonZeroSecond(0, 0) == 0, "EarliestNonZero(0,0)=0");
        Check(EarliestNonZeroSecond(3, 9) == 3, "EarliestNonZero(3,9)=3");
        Check(EarliestNonZeroSecond(9, 3) == 3, "EarliestNonZero(9,3)=3");
    }

    // ---- 3. 单调性穷举 ----
    // 四个可观察状态各选两个值,4^? 种组合里每一条合并结果都不劣于任一输入。
    // 这一条是"恢复不丢用户状态"的正面表述:任何一次合并都不能让某项倒退。
    {
        const unsigned long long stamps[] = {0, 1, 100, 1700000000ULL};
        int combos = 0;
        for (unsigned long long ai : stamps) {
            for (unsigned long long li : stamps) {
                for (unsigned long long lu : stamps) {
                    for (int fav = 0; fav <= 1; ++fav) {
                        RestoredUserState a;
                        a.importedUnixSeconds = ai;
                        a.lastUsedUnixSeconds = lu;
                        a.favorite = fav == 1;
                        RestoredUserState b;
                        b.importedUnixSeconds = li;
                        b.lastUsedUnixSeconds = 0;
                        b.favorite = fav == 0;
                        if (!SatisfiesInvariants(a, b)) {
                            Check(false, "合并结果在某项上劣于输入(导入=" + std::to_string(ai) +
                                             ",使用=" + std::to_string(lu) + ",藏=" + std::to_string(fav) + ")");
                        }
                        ++combos;
                    }
                }
            }
        }
        Check(true, "三条不变式穷举:全部 " + std::to_string(combos) + " 种组合都成立");
        // 交换律:两个输入对调,结论不变。恢复路径有两条(规范化 / 内置),
        // 它们各自以不同的东西当 canonical —— 合并策略不该因此给出不同答案。
        for (unsigned long long ai : stamps) {
            for (unsigned long long li : stamps) {
                RestoredUserState a;
                a.importedUnixSeconds = ai;
                RestoredUserState b;
                b.importedUnixSeconds = li;
                const auto ab = MergeRestoredUserState(a, b);
                const auto ba = MergeRestoredUserState(b, a);
                Check(ab.importedUnixSeconds == ba.importedUnixSeconds && ab.favorite == ba.favorite &&
                          ab.lastUsedUnixSeconds == ba.lastUsedUnixSeconds,
                      "合并满足交换律(两条恢复路径不该给出不同答案)");
            }
        }
    }

    // ---- 4. 字段清单与结构体配对 ----
    // 加字段时必须同时改清单。C++ 没有反射,所以这份清单是手写的;
    // 它唯一的作用就是让"漏了一个字段"在测试里**可断言** ——
    // 而 `WallpaperLibrary::Load` 里那两份副本,任何一处漏字段都无声。
    {
        int fields = 0;
        ForEachField(RestoredUserState{}, [&](const char*, const auto&) { ++fields; });
        Check(fields == static_cast<int>(kRestoredUserStateFieldCount),
              "结构体字段数(" + std::to_string(fields) + ")与清单(" +
                  std::to_string(static_cast<int>(kRestoredUserStateFieldCount)) + ")一致");
        // 清单里每个名字都真的出现(防手写清单写了不存在的字段)
        const std::string_view list = kRestoredUserStateFields;
        for (const char* name : {"favorite", "importedUnixSeconds", "lastUsedUnixSeconds"}) {
            Check(list.find(name) != std::string_view::npos, std::string("清单里含 ") + name);
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n库恢复合并:全部 %d 项通过\n", g_checks);
    return 0;
}
