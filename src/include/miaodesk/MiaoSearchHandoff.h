#pragma once

// SEARCH-05「Search → AI 连续上下文」里"搜索框的词怎么进 AI 面板"的那一下。
//
// 用户在搜索框里打一段话，按 Enter 交给妙喵 AI。`ShowL3CliWindow` 有两条路：
// 面板已经开着（复用），或者要新建。两条路都要回答同一个问题：**这段词现在怎么办**。
//
// 复用那一路原来的写法是：
//
//     if (!Trim(initialPrompt).empty() && !busy && !pendingConfirmation) {
//         SetWindowTextW(input, prompt); SendPrompt(state);
//     }
//
// 于是面板正忙、或者在等用户确认某个待定操作时，**这段词既不进输入框也不发出去** ——
// 它整段消失。用户看到的是 AI 窗口被带到前台，输入框空空如也，什么也没发生。
// SEARCH-05 的验收原话是"原查询自然成为对话上下文"；而这里它什么都不是。
//
// 这里只放那个判定，不碰窗口、不碰输入框、不碰模型。
#include <string>

namespace miaodesk {

// 搜索框那段词接下来怎么办。
enum class SearchHandoffAction {
    Drop,           // 什么都不做(调用方只负责把面板带到前台)
    PrefillOnly,    // 只填进输入框，不自动发
    PrefillAndSend, // 填进输入框并当场发出去
};

// 判定该走哪一条。
//
// 一条不变式：**非空的词永不被丢下**。空词没什么可带的，调用方照旧把面板带到前台、
// 交出焦点就完了；而非空的词是用户刚打的一段话，丢了它界面看起来就是"点了没反应"。
// 面板忙、或在等确认时，自动发是不安全的（会把第二轮接在一轮还没完的会话后面），
// 但**填进输入框永远安全** —— 用户等到那一轮结束，自己按一下 Enter 就行。
SearchHandoffAction DecideSearchHandoff(bool promptHasText,
                                        bool panelBusy,
                                        bool awaitingConfirmation) noexcept;

} // namespace miaodesk
