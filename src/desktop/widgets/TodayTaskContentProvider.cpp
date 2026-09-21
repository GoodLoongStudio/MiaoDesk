#include "miaodesk/TodayTaskContentProvider.h"

#include "miaodesk/TodayTaskStore.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace miaodesk::desktop {
namespace {

constexpr std::size_t kPublishedTaskSlots = 4;

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
