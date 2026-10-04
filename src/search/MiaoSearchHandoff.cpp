#include "miaodesk/MiaoSearchHandoff.h"

namespace miaodesk {

SearchHandoffAction DecideSearchHandoff(bool promptHasText,
                                        bool panelBusy,
                                        bool awaitingConfirmation) noexcept {
    // 空词没什么可带的。调用方照旧把面板带到前台、交出焦点。
    if (!promptHasText) return SearchHandoffAction::Drop;

    // 有词，但面板正忙或在等用户确认：自动发会把第二轮接在一轮还没完的会话后面。
    // 填进输入框仍然安全 —— 那一轮结束后用户自己按 Enter 就行。
    if (panelBusy || awaitingConfirmation) return SearchHandoffAction::PrefillOnly;

    return SearchHandoffAction::PrefillAndSend;
}

} // namespace miaodesk
