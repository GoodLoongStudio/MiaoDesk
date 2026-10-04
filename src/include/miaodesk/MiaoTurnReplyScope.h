#pragma once

// AI 面板那条三轮消息路径上的代号算术。
//
// `ConversationPanelImpl.inc` 里三个消息(kDeltaMessage / kPiDoneMessage /
// kDirectDoneMessage)都要回答同一个问题:**这条回包还属于当前这一轮吗**。原写法是三处
// 一字不差的副本:
//
//     if (!payload || payload->generation != state->generation
//                   || payload->generation != gCliGeneration.load(...)) return 0;
//
// 三方比对,不是两方:既要比"产生它的那一轮还是面板当前这一轮",也要比"那一轮还是
// 最新起过的那一轮"。少任何一边都会出用户看得见的事故 ——
//
//   · 只看 turn:用户取消后立刻重试,旧轮的 delta 会被追加进新一轮的条目,
//     于是用户看到自己没说过的话从模型嘴里说出来;
//   · 只看 latest:面板已经切到另一轮,那条 delta 仍然会落到**旧那一轮**的条目上,
//     表现为"回复串台"。
//
// 而这行判定住在 `ConversationPanelImpl.inc`(要 `<windows.h>`),本机一行都跑不到。
// 这是同一套模式在仓库里的**第三份**副本:第一份是搜索侧文件查询的代号
// (`MiaoSearchGeneration`),第二份是搜索框 → AI 的交接(`MiaoSearchHandoff`)。
// 前两份已经提成本机可测;这一份此前写在面板上的"下一轮该做"里,现在做掉。
//
// 这里只放比对,不碰窗口、不碰消息。
#include <cstdint>

namespace miaodesk {

// 这条回包还该不该被采用。
//
// turnGeneration  面板当前这一轮的代号(`state->generation`)
// latestGeneration 最新起过的那一轮的代号(`gCliGeneration`,进程级)
//
// 两个都必须等于回包自己的代号。理由见上面那段:少任何一边都有一种串台事故。
bool TurnReplyIsCurrent(std::uint64_t claimedGeneration,
                        std::uint64_t turnGeneration,
                        std::uint64_t latestGeneration) noexcept;

} // namespace miaodesk
