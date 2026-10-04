#include "miaodesk/MiaoTurnReplyScope.h"

namespace miaodesk {

bool TurnReplyIsCurrent(std::uint64_t claimedGeneration,
                        std::uint64_t turnGeneration,
                        std::uint64_t latestGeneration) noexcept {
    // 两个都必须相等。任一边松掉都会串台,两边松掉则等于没有这道门。
    return claimedGeneration == turnGeneration && claimedGeneration == latestGeneration;
}

} // namespace miaodesk
