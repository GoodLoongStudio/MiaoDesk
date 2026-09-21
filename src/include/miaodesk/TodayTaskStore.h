#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::desktop {

struct TodayTaskItem {
    std::wstring id;
    std::wstring title;
    std::wstring detail;
    bool completed{};
};

struct TodayTaskSnapshot {
    bool valid{};
    std::uint64_t generation{};
    std::vector<TodayTaskItem> items;
    std::size_t total{};
    std::size_t completed{};
    std::size_t pending{};
};

class TodayTaskStore {
public:
    static bool Load(TodayTaskSnapshot* snapshot, std::wstring* error = nullptr);
    static bool Replace(const std::vector<TodayTaskItem>& items, std::wstring* error = nullptr);
    static bool SetCompleted(std::wstring_view id, bool completed, std::wstring* error = nullptr);

    static std::filesystem::path StorePath();
    static bool SelfTest();
};

} // namespace miaodesk::desktop
