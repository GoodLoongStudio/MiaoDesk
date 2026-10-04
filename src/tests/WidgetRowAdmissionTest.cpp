// P0-09「升级不丢组件布局」的入账判定回归。
//
// 逮到的不是"它会算错",而是**它在本机一行都验不到,而且五种结局被压成一个 continue**:
// `DesktopWidgetStore::Load` 里
//     if (kind == Unknown || source.empty() || !IsValidPersistedSource(widget)) continue;
// 三种完全不同的原因共用一个 continue,而 Load 照常返回 true —— "升级不丢组件布局"
// 于是变成"丢了几条并报告成功"。撞 singleton 键时的两种去重结局也压在同一行里。
//
// 这里钉住:五种结局各自可达、各有各的说法,以及撞键时"启用状态优先、同状态先出现者胜"。
#include "miaodesk/MiaoWidgetRowAdmission.h"

#include <cstdio>
#include <string>

namespace miaodesk {
namespace widget_row {
namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL  %s\n", what.c_str());
    }
}

WidgetRowFacts Healthy() {
    WidgetRowFacts f;
    f.kindRecognised = true;
    f.sourcePresent = true;
    f.sourceValid = true;
    f.hasSingletonKey = false;
    f.singletonClashes = false;
    f.candidateEnabled = true;
    return f;
}

// 反空洞自检。两个方向各喂一个:一个恒 Admit 的判定会把坏行收进库
// (用户看到莫名组件),一个恒 KeepExisting 的判定会让新行永远进不去。
bool VerdictStillMoves() {
    const WidgetRowFacts good = Healthy();
    WidgetRowFacts broken = Healthy();
    broken.kindRecognised = false;
    return DecideWidgetRowAdmission(good) == WidgetRowOutcome::Admit &&
           DecideWidgetRowAdmission(broken) == WidgetRowOutcome::RejectUnknownKind;
}

} // namespace
} // namespace widget_row
} // namespace miaodesk

int wmain() {
    using namespace miaodesk::widget_row;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:入账判定不动\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:好行收下、坏行拒掉\n");
    ++g_checks;

    // ---- 1. 五种结局各自可达 ----
    {
        Check(DecideWidgetRowAdmission(Healthy()) == WidgetRowOutcome::Admit, "healthy → Admit");

        WidgetRowFacts f = Healthy(); f.kindRecognised = false;
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::RejectUnknownKind, "Kind 不认识 → RejectUnknownKind");

        f = Healthy(); f.sourcePresent = false;
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::RejectMissingSource, "Source 空 → RejectMissingSource");

        f = Healthy(); f.sourceValid = false;
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::RejectUnsafeSource, "Source 不受认 → RejectUnsafeSource");

        f = Healthy(); f.hasSingletonKey = true; f.singletonClashes = true;
        f.existingEnabled = false; f.candidateEnabled = true;
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::SupersedeDisabled, "撞键且库里禁用 → SupersedeDisabled");

        f = Healthy(); f.hasSingletonKey = true; f.singletonClashes = true;
        f.existingEnabled = true; f.candidateEnabled = false;
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::KeepExisting, "撞键且库里启用 → KeepExisting");
    }

    // ---- 2. 三种拒绝有先后,且互不遮蔽 ----
    // 三个条件同时坏时必须报**最具体**的那个:Kind 不认识时连 Source 是什么都还没法解释。
    {
        WidgetRowFacts f;  // 全 false
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::RejectUnknownKind,
              "三样全坏时先报 Kind(它最靠前,也最解释得通)");
        f.kindRecognised = true;
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::RejectMissingSource,
              "Kind 认得但 Source 空 → 报 MissingSource");
        f.sourcePresent = true;
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::RejectUnsafeSource,
              "Source 有了但不受认 → 报 UnsafeSource(这是安全边界,不与损坏混为一类)");
    }

    // ---- 3. 撞键:启用状态优先 ----
    {
        WidgetRowFacts f = Healthy();
        f.hasSingletonKey = true; f.singletonClashes = true;
        f.existingEnabled = false; f.candidateEnabled = true;
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::SupersedeDisabled,
              "库里禁用 + 本条启用 → 替换(用户主动启用过,那是最新意图)");
    }

    // ---- 4. 撞键:同状态先出现者胜 ----
    // 四种同状态组合,全部保留库里那条。
    {
        const bool states[] = {false, true};
        for (bool existing : states) {
            for (bool candidate : states) {
                if (!existing && candidate) continue;  // 这一种上面已覆盖(替换)
                WidgetRowFacts f = Healthy();
                f.hasSingletonKey = true;
                f.singletonClashes = true;
                f.existingEnabled = existing;
                f.candidateEnabled = candidate;
                Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::KeepExisting,
                      std::string("撞键同状态(existing=") + (existing ? "1" : "0") + ",candidate=" +
                          (candidate ? "1" : "0") + ") → 保留先出现的那条");
            }
        }
    }

    // ---- 5. Content 组件不参与去重(键为空) ----
    // 同一个定义在多块屏幕上各放一个是允许的,所以 Content 行的 hasSingletonKey
    // 恒假 —— 即便库里已有一条一模一样的,也必须照收。
    {
        WidgetRowFacts f = Healthy();
        f.hasSingletonKey = false;
        f.singletonClashes = true;   // 调用方误报撞键也不该改变结论
        Check(DecideWidgetRowAdmission(f) == WidgetRowOutcome::Admit,
              "没有 singleton 键时即便报撞键也照收(Content 组件允许多开)");
    }

    // ---- 6. 只有真的改变了库才算变化 ----
    // 调用方靠这个决定要不要重写 INI。判宽了会每次都重写,判严了会让去重不落盘。
    {
        Check(WidgetRowOutcomeMutatesStore(WidgetRowOutcome::Admit), "Admit 改变库");
        Check(WidgetRowOutcomeMutatesStore(WidgetRowOutcome::SupersedeDisabled), "SupersedeDisabled 改变库");
        Check(!WidgetRowOutcomeMutatesStore(WidgetRowOutcome::RejectUnknownKind), "拒绝不改变库");
        Check(!WidgetRowOutcomeMutatesStore(WidgetRowOutcome::RejectMissingSource), "拒绝不改变库");
        Check(!WidgetRowOutcomeMutatesStore(WidgetRowOutcome::RejectUnsafeSource), "拒绝不改变库");
        Check(!WidgetRowOutcomeMutatesStore(WidgetRowOutcome::KeepExisting), "撞键保留原样不算改变库");
    }

    // ---- 7. 五种结局各有各的说法 ----
    // 用户看得见的就是这句话,所以它必须存在、必须互不相同、必须指名那一条。
    {
        const WidgetRowOutcome all[] = {
            WidgetRowOutcome::Admit, WidgetRowOutcome::RejectUnknownKind,
            WidgetRowOutcome::RejectMissingSource, WidgetRowOutcome::RejectUnsafeSource,
            WidgetRowOutcome::SupersedeDisabled, WidgetRowOutcome::KeepExisting};
        std::wstring seen;
        for (auto o : all) {
            const std::wstring text = ExplainWidgetRowOutcome(o);
            if (o == WidgetRowOutcome::Admit) {
                Check(text.empty(), "Admit 没有要说的话(本来就该进)");
                continue;
            }
            Check(!text.empty(), std::string("结局 ") + ToString(o) + " 有话说");
            Check(seen.find(text) == std::string::npos,
                  std::string("结局 ") + ToString(o) + " 的说法独一无二");
            Check(text.find(L"已跳过") != std::wstring::npos || text.find(L"已保留") != std::wstring::npos ||
                      text.find(L"已用") != std::wstring::npos,
                  std::string("结局 ") + ToString(o) + " 说明了到底怎么处理的");
            seen += text;
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n组件行入账:全部 %d 项通过\n", g_checks);
    return 0;
}
