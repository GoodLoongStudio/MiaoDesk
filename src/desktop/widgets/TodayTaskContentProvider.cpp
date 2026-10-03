#include "miaodesk/TodayTaskContentProvider.h"

#include "miaodesk/TodayTaskStore.h"
#include "miaodesk/TodayTaskPresentation.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace miaodesk::desktop {
namespace {

// 槽位数与原生卡片共用一份(kTodayTaskVisibleSlots)。这里曾经是一个局部的 4 ——
// 两处各写一个,迟早对不上。
constexpr std::size_t kPublishedTaskSlots = kTodayTaskVisibleSlots;

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

void Put(content::ContentDataValues& values, std::wstring key, content::ContentDataValue value) {
    values.insert_or_assign(std::move(key), std::move(value));
}

} // namespace

bool TodayTaskContentProvider::Capture(content::ContentDataValues* values, std::wstring* error) {
    if (!values) return Fail(error, L"Today Tasks Content data 输出不能为空。");
    values->clear();

    TodayTaskSnapshot snapshot;
    std::wstring loadError;
    const bool loaded = TodayTaskStore::Load(&snapshot, &loadError) && snapshot.valid;

    Put(*values, L"tasks.available", loaded);
    Put(*values, L"tasks.total", static_cast<std::int64_t>(loaded ? snapshot.total : 0));
    Put(*values, L"tasks.completed", static_cast<std::int64_t>(loaded ? snapshot.completed : 0));
    Put(*values, L"tasks.pending", static_cast<std::int64_t>(loaded ? snapshot.pending : 0));
    Put(*values, L"tasks.progressText",
        loaded ? std::to_wstring(snapshot.completed) + L" / " + std::to_wstring(snapshot.total) + L" 完成"
               : std::wstring(L"任务数据暂不可用"));
    Put(*values, L"tasks.emptyText",
        loaded && snapshot.items.empty() ? std::wstring(L"还没有今日待办") :
        (!loaded ? (loadError.empty() ? std::wstring(L"任务数据暂不可用") : loadError) : std::wstring{}));

    // 只有固定槽位,而用户的待办可以多于 4 条。此前顶部的计数说的是真话
    // ("8 项待办"),下面却只画 4 行,而且没有任何地方告诉用户"还有 4 条" ——
    // 用户以为组件坏了,或者以为自己只加了 4 条。沉默地截断和显示假数据是同一类错:
    // 组件都在对自己画出来的东西撒谎。
    // 这一行给内容一个可以绑定的溢出说明;内容不绑它也不构成谎言(那时什么都不显示),
    // 但绑了它就必须是真的。
    Put(*values, L"tasks.overflowText",
        loaded ? TaskOverflowText(snapshot.items.size(), kTodayTaskVisibleSlots) : std::wstring{});

    for (std::size_t i = 0; i < kPublishedTaskSlots; ++i) {
        const bool present = loaded && i < snapshot.items.size();
        const TodayTaskItem* item = present ? &snapshot.items[i] : nullptr;
        const std::wstring prefix = L"tasks.item" + std::to_wstring(i) + L".";
        Put(*values, prefix + L"present", present);
        Put(*values, prefix + L"id", item ? item->id : std::wstring{});
        Put(*values, prefix + L"title", item ? item->title : std::wstring{});
        Put(*values, prefix + L"detail", item ? item->detail : std::wstring{});
        Put(*values, prefix + L"completed", item ? item->completed : false);
        Put(*values, prefix + L"marker", item ? (item->completed ? std::wstring(L"✓") : std::wstring(L"○")) : std::wstring{});
    }

    if (error) *error = loaded ? std::wstring{} : loadError;
    return true;
}

bool TodayTaskContentProvider::SelfTest() {
    content::ContentDataValues values;
    std::wstring error;
    if (!Capture(&values, &error)) return false;
    const auto pending = values.find(L"tasks.pending");
    const auto marker = values.find(L"tasks.item0.marker");
    return pending != values.end() && std::holds_alternative<std::int64_t>(pending->second) &&
           marker != values.end() && std::holds_alternative<std::wstring>(marker->second);
}

} // namespace miaodesk::desktop
