#include "miaodesk/MiaoProfileSwitchGuard.h"

namespace miaodesk::profile_switch {

ProfileSelectorAction DecideProfileSelectorAction(bool preserveSelection,
                                                  bool piBusy,
                                                  bool windowBusy) noexcept {
    // 首次填充:随便重建。这个窗口还没有轮次,用户也没有做过选择。
    if (!preserveSelection) return ProfileSelectorAction::Rebuild;
    // 运行期刷新:任一忙位为真就不动。两个忙位取或而不是只看 piBusy ——
    //  SetBusyVisual(state, true) 发生在 AskAsync 之前,busy 已真而 Pi 还不忙
    //  的那段间隙是真实存在的,而重建落在那里等于 Stop() + conversation_.clear()。
    // 共享运行时被别的窗口占着时同理:这个窗口动它的下拉,会让"以为自己选了 X、
    // 其实跑的是 Y"出现。
    if (piBusy || windowBusy) return ProfileSelectorAction::Keep;
    return ProfileSelectorAction::Rebuild;
}

std::wstring ExplainProfileSelectorAction(ProfileSelectorAction action) {
    switch (action) {
        case ProfileSelectorAction::Rebuild: return L"";
        case ProfileSelectorAction::Keep:
            return L"当前有 AI 任务正在执行，任务结束后再切换 API。";
    }
    return L"";
}

} // namespace miaodesk::profile_switch
