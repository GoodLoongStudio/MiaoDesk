#pragma once

// D-2:原生待办卡片的**行模型**。
//
// 为什么单独一份:PaintTodayTasks 原来在 painter 里手写三条待办 ——
// {完成产品设计方案, false}、{与团队同步项目进度, false}、{回复客户邮件, true},
// 外加一个写死的 L"3" 与 "1 / 3 完成"。它从不读 TodayTaskStore,于是用户在管理界面
// 认认真真编辑的待办,在那个卡片上一个都不会出现,而卡片看起来完全正常。
//
// 一个常驻桌面的组件显示**不存在的数据**,比显示错误更糟:错误会促使用户去修,
// 假数据只会让人以为自己的待办已经同步好了。
//
// 所以形状(几行、每行显示什么、进度怎么算、没有数据时说什么)全都在这里定,
// 而它是纯逻辑 —— 不碰盘、不 import Windows 头,于是"没有数据就说没有数据"这条
// 在本机就能真验,不需要一台装了 Windows 的机器。
#include <cstddef>
#include <string>
#include <vector>

#include "miaodesk/TodayTaskStore.h"

namespace miaodesk::desktop {

struct TodayTaskRow {
    bool present{};
    std::wstring title;
    std::wstring detail;
    bool completed{};
};

struct TodayTaskCardModel {
    // 快照可用吗。false 时下面每一个数字都不该被显示成"0 项待办" ——
    // 那是把"读不到"说成"没有待办",两件事对用户的意义完全不同。
    bool valid{};
    std::size_t total{};
    std::size_t completed{};
    std::size_t pending{};
    // 0..1;没有待办时是 0 而不是 1 —— 空进度的满格会把空态画成"全部完成"。
    float progress{};
    // 给用户看的一句话:有数据时报数量,没有数据时报为什么没有。
    std::wstring statusText;
    // 最多 maxRows 行。放不下就放不下,不滚动、不裁字 —— 那条属于 WPRO-02。
    std::vector<TodayTaskRow> rows;
};

// 从真实快照构造卡片模型。
//
// maxRows 是卡片画得下的行数,由调用方按画布给。snapshot 无效时返回 valid=false,
// statusText 说明原因,rows 为空 —— 调用方不得用别的东西填满它。
TodayTaskCardModel BuildTodayTaskCardModel(const TodayTaskSnapshot& snapshot, std::size_t maxRows);

} // namespace miaodesk::desktop
