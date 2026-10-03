#include "miaodesk/TodayTaskPresentation.h"

namespace miaodesk::desktop {
namespace {

// 数数的口径与 TodayTaskContentProvider 保持一致:那边是内容组件的真实来源,
// 这边是原生卡片的真实来源。两处各写一个"完成 / 总数"的格式,迟早显示成两个数。
std::wstring ProgressText(const TodayTaskSnapshot& snapshot) {
    return std::to_wstring(snapshot.completed) + L" / " + std::to_wstring(snapshot.total) + L" 完成";
}

} // namespace

TodayTaskCardModel BuildTodayTaskCardModel(const TodayTaskSnapshot& snapshot, std::size_t maxRows) {
    TodayTaskCardModel model;
    if (!snapshot.valid) {
        // 读不到就说读不到。这里不产生"3 项待办"—— 那是编一个用户没有的状态,
        // 而用户会以为自己的待办已经同步好了。
        model.valid = false;
        model.statusText = L"任务数据暂不可用";
        return model;
    }

    model.valid = true;
    model.total = snapshot.total;
    model.completed = snapshot.completed;
    model.pending = snapshot.pending;
    model.progress = snapshot.total == 0
                         ? 0.0f
                         : static_cast<float>(snapshot.completed) /
                               static_cast<float>(snapshot.total);
    model.statusText = snapshot.items.empty()
                           ? std::wstring(L"还没有今日待办")
                           : std::to_wstring(snapshot.total) + L" 项待办";

    model.rows.reserve(maxRows);
    for (std::size_t i = 0; i < maxRows && i < snapshot.items.size(); ++i) {
        const auto& item = snapshot.items[i];
        TodayTaskRow row;
        row.present = true;
        row.title = item.title;
        row.detail = item.detail;
        row.completed = item.completed;
        model.rows.push_back(std::move(row));
    }
    return model;
}

} // namespace miaodesk::desktop
