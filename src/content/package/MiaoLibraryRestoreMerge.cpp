#include "miaodesk/MiaoLibraryRestoreMerge.h"

namespace miaodesk::library_restore {

unsigned long long EarliestNonZeroSecond(unsigned long long a, unsigned long long b) noexcept {
    // 0 是"没有记录",不是"1970 年 1 月 1 日"。当成 1970 会让每一次
    // "导入时间缺失"的记录都赢得"更早",于是真实导入时间被它盖掉。
    if (a == 0) return b;
    if (b == 0) return a;
    return a < b ? a : b;
}

RestoredUserState MergeRestoredUserState(const RestoredUserState& canonical,
                                        const RestoredUserState& legacy) noexcept {
    RestoredUserState merged;
    // 收藏是 or,不是覆盖:任一侧收藏过就收藏。覆盖会让"在另一份里收藏过"
    // 这个事实消失,而用户不记得自己是在哪一份里点的。
    merged.favorite = canonical.favorite || legacy.favorite;
    // 导入时间取更早的非零:一条壁纸被导入过两次(两次迁移各留一行),
    // 它的导入时间该是第一次,不是最后一次。
    merged.importedUnixSeconds =
        EarliestNonZeroSecond(canonical.importedUnixSeconds, legacy.importedUnixSeconds);
    // 最近使用取更大:刚用过的那次才是"最近"。
    merged.lastUsedUnixSeconds =
        canonical.lastUsedUnixSeconds > legacy.lastUsedUnixSeconds ? canonical.lastUsedUnixSeconds
                                                                  : legacy.lastUsedUnixSeconds;
    return merged;
}

} // namespace miaodesk::library_restore
