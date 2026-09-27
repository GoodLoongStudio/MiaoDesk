// CCA-10:关掉创作窗口再打开,要能接上。
//
// 这份测试的重点是**"恢复一半"看起来一切正常**。窗口打开着、有内容、没有报错,
// 而里面那一句需求不是用户说过的那一句 —— 用户会以为是自己记错了,然后照着
// 错的做下去。
//
// 所以几条断言各自钉住一个会让"恢复一半"发生的形状:
//   * 多行需求(换行 / 等号):不转义的话,第二行会被解析成别的键;
//   * 认不出的字段:不报错,加字段不该让所有人的旧草稿全失效;
//   * 缺版本行:不是草稿,不能当成一份空草稿恢复;
//   * sessionId 不符:那是另一个作品的草稿,不能当成本作的新窗口;
//   * 需求不足:必须说清缺哪一项;"需求不足"四个字等于让用户猜自己上次漏了什么;
//   * 取消过:只显示,不自动继续 —— 取消之后自己动起来,是他取消没生效。
//
// 顺带钉住往返:序列化再解析回来必须逐字段相同,包括那一段多行需求原文。
#include "miaodesk/CreationDraftStore.h"

#include <cstdio>
#include <string>
#include <vector>

namespace miaodesk::creator {
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

void CheckEq(const std::string& actual, const std::string& expected, const std::string& what) {
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL  %s\n        expected: %s\n        actual:   %s\n",
                    what.c_str(), expected.c_str(), actual.c_str());
    }
}

const char* kSession = "S-1";

// 一段**完整**的草稿:种类有、目标有,于是可以恢复并继续。
CreationDraft FullDraft() {
    CreationDraft draft;
    draft.sessionId = kSession;
    draft.epoch = 3;
    draft.turnId = 7;
    draft.briefRevision = 2;
    draft.kind = ContentCreatorKind::Wallpaper;
    draft.goal = "安静的夜色桌面\n云慢慢飘,不要弹窗";
    draft.visualDirection = "低饱和蓝";
    draft.aspectOrSize = "2560x1440";
    draft.dataAndInteraction = "只要时钟";
    draft.materialSource = "包内自带";
    draft.allowedCapabilities = {"clock.read"};
    draft.userParameters = {{"speed", "0.5"}, {"hue", "0.62"}};
    draft.candidateDigest = std::string(64, 'a');
    draft.candidateSummary = "第一版候选";
    draft.candidateRevision = 1;
    draft.cancelRequested = false;
    draft.savedAtMs = 1755000000;
    return draft;
}

std::string ParseError(const std::string& text) {
    CreationDraft draft;
    std::string error;
    if (ParseCreationDraft(text, &draft, &error)) return {};
    return error;
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;

    // --- 1. 往返:序列化再读回来,一个字段都不能差 ---------------------------
    {
        const CreationDraft original = FullDraft();
        const std::string text = SerializeCreationDraft(original);
        CreationDraft restored;
        std::string error;
        Check(ParseCreationDraft(text, &restored, &error), "完整草稿能读回来");
        CheckEq(restored.goal, original.goal, "多行需求原文一致 —— 少一行就会照着错的做");
        CheckEq(restored.visualDirection, original.visualDirection, "视觉方向一致");
        CheckEq(restored.aspectOrSize, original.aspectOrSize, "尺寸一致");
        CheckEq(restored.dataAndInteraction, original.dataAndInteraction, "数据与交互一致");
        CheckEq(restored.materialSource, original.materialSource, "素材来源一致");
        CheckEq(restored.candidateDigest, original.candidateDigest, "上一有效候选的摘要一致");
        CheckEq(restored.candidateSummary, original.candidateSummary, "摘要一致");
        Check(restored.candidateRevision == 1, "候选版本号一致");
        Check(restored.briefRevision == 2, "需求版本号一致");
        Check(restored.epoch == 3, "epoch 一致");
        Check(restored.turnId == 7, "轮次一致");
        Check(restored.savedAtMs == 1755000000, "保存时刻一致");
        Check(restored.kind == ContentCreatorKind::Wallpaper, "种类一致");
        Check(restored.allowedCapabilities.size() == 1 &&
                  restored.allowedCapabilities[0] == "clock.read",
              "能力声明一致");
        Check(restored.userParameters.size() == 2, "用户参数一条都不少");
        Check(restored.userParameters.size() == 2 &&
                  restored.userParameters[0] == std::make_pair(std::string("speed"),
                                                               std::string("0.5")),
              "第一个用户参数是 speed=0.5");
        Check(restored.userParameters.size() == 2 &&
                  restored.userParameters[1] == std::make_pair(std::string("hue"),
                                                               std::string("0.62")),
              "第二个用户参数是 hue=0.62");
        // 转义本身要正确:文本里不得出现裸换行出现在值里被当成行分隔的情况之外的错。
        Check(text.find("goal=\\\\n") == std::string::npos, "换行被转义,不是原样留在值里");
    }

    // 用户参数与方法里含逗号/等号:分隔符要靠转义兜住。
    {
        CreationDraft draft = FullDraft();
        draft.userParameters = {{"note", "a,b=c"}, {"empty", ""}};
        const std::string text = SerializeCreationDraft(draft);
        CreationDraft restored;
        std::string error;
        Check(ParseCreationDraft(text, &restored, &error), "含逗号与等号的参数也能读回来");
        Check(restored.userParameters.size() == 2, "还是两条参数 —— 逗号没有把它们切开");
        Check(restored.userParameters.size() == 2 &&
                  restored.userParameters[0] == std::make_pair(std::string("note"),
                                                               std::string("a,b=c")),
              "值里的逗号与等号原样回来");
    }

    // --- 2. 完整草稿 → 恢复并继续,不必再问 ----------------------------------
    {
        const CreationDraft original = FullDraft();
        const auto plan = PlanDraftRestore(SerializeCreationDraft(original), kSession);
        Check(plan.action == DraftRestoreAction::RestoreAndResume, "完整草稿 → 接上继续");
        Check(plan.draft.goal == original.goal, "带回来的是用户原话");
        Check(plan.reason.find("不必再问") != std::string::npos, "并说明不必再问一遍");
        CheckEq(std::string(ToString(plan.action)), "RestoreAndResume", "动作名稳定");
    }

    // --- 3. 没有草稿 → 从新的开始 -------------------------------------------
    {
        const auto plan = PlanDraftRestore("", kSession);
        Check(plan.action == DraftRestoreAction::StartFresh, "没有草稿 → 从新开始");
        CheckEq(std::string(ToString(plan.action)), "StartFresh", "动作名稳定");
    }

    // --- 4. 取消过 → 只显示,不自动继续 --------------------------------------
    {
        auto draft = FullDraft();
        draft.cancelRequested = true;
        const auto plan = PlanDraftRestore(SerializeCreationDraft(draft), kSession);
        Check(plan.action == DraftRestoreAction::RestoreAsDraft, "取消过 → 只显示草稿");
        Check(plan.draft.goal == draft.goal, "内容仍然显示回来");
        Check(plan.reason.find("取消") != std::string::npos, "并说明它是被取消过的那一份");
        CheckEq(std::string(ToString(plan.action)), "RestoreAsDraft", "动作名稳定");
    }

    // --- 5. 需求不足 → 要再说问,而且说清缺哪一项 -----------------------------
    {
        auto draft = FullDraft();
        draft.goal.clear();
        const auto plan = PlanDraftRestore(SerializeCreationDraft(draft), kSession);
        Check(plan.action == DraftRestoreAction::AskUserAgain, "缺目标 → 再问一次");
        Check(plan.reason.find("目标") != std::string::npos, "并说清缺的是目标");
        Check(plan.draft.visualDirection == draft.visualDirection, "别的一并带回来,别白问");
        Check(draft.Sufficient() == false, "Sufficient 判据:缺目标不算足够");
        Check(draft.MissingReason() == "你的目标(想要什么样的桌面效果)", "缺的原因点名是目标");

        auto noKind = FullDraft();
        noKind.kind = ContentCreatorKind::None;
        Check(!noKind.Sufficient(), "缺种类也不算足够");
        Check(noKind.MissingReason() == "制作类型(壁纸 / 组件)", "缺种类时说清缺的是种类");
        Check(FullDraft().MissingReason().empty(), "什么都不缺时原因为空");
    }

    // --- 6. 接不上的草稿:绝不当成新草稿 -------------------------------------
    {
        const auto notADraft = PlanDraftRestore("sessionId=S-1\nturnId=4\n", kSession);
        Check(notADraft.action == DraftRestoreAction::RejectAmbiguous, "没有版本行 → 接不上");
        Check(notADraft.reason.find("不是一份草稿") != std::string::npos, "并说明它根本不是草稿");
        CheckEq(std::string(ToString(notADraft.action)), "RejectAmbiguous", "动作名稳定");

        const auto foreign = PlanDraftRestore(SerializeCreationDraft(FullDraft()), "S-2");
        Check(foreign.action == DraftRestoreAction::RejectAmbiguous, "别的工作会话的草稿 → 接不上");
        Check(foreign.reason.find("S-1") != std::string::npos && foreign.reason.find("S-2") != std::string::npos,
              "并把两边的工作名都说出来");

        // 坏数字:不能静默读成 0。turnId=0 的样子和一个从未开始过的会话一样,
        // 而恢复出来的草稿会让"旧轮次不能覆盖新结果"整段失效。
        Check(!ParseError("draft=1\nsessionId=S-1\nturnId=abc\n").empty(), "坏 turnId 被拒");
        // 坏 kind:0/1/2 之外没有对应种类。
        Check(!ParseError("draft=1\nsessionId=S-1\nkind=7\n").empty(), "kind=7 被拒");
        // 不是一个 key=value 行。
        Check(!ParseError("draft=1\nsessionId=S-1\n这一行没有等号\n").empty(), "坏行被拒");
        // 缺 sessionId:无从归属。
        Check(!ParseError("draft=1\nturnId=4\n").empty(), "缺 sessionId 被拒");
    }

    // 解析失败时 *out 不被信任:它可能已经装了一半。调用方必须按"接不上"处理,
    // 而不是按"一个空草稿"处理 —— 后者会让用户看到一个全新窗口,
    // 而他记得自己写了一整段需求,他会以为是自己记错了。
    {
        CreationDraft draft = FullDraft();
        const std::string error = ParseError("draft=1\nsessionId=S-1\ngoal=有目标\nturnId=坏\n");
        Check(!error.empty(), "半路坏掉也返回 false");
        Check(error.find("turnId") != std::string::npos, "错误点明是哪一行坏掉");
    }

    // --- 7. 认不出的字段:不报错 --------------------------------------------
    {
        // 新版本加一个字段时,旧版本的代码还要能读。报错会让"加一个字段"
        // 变成"所有人的旧草稿全失效"。
        const std::string future = SerializeCreationDraft(FullDraft()) + "futureField=随便\n";
        CreationDraft restored;
        std::string error;
        Check(ParseCreationDraft(future, &restored, &error), "多一个不认识的字段也能读");
        Check(restored.goal == FullDraft().goal, "且内容不丢");
        // 但版本行错了必须拒:那不是同一份草稿的新版本,是别的东西。
        std::string wrongVersion = SerializeCreationDraft(FullDraft());
        const auto at = wrongVersion.find("draft=1");
        wrongVersion.replace(at, 7, "draft=2");
        Check(!ParseError(wrongVersion).empty(), "版本不是 1 被拒");
    }

    // --- 8. 空目标与空值 ---------------------------------------------------
    {
        CreationDraft draft = FullDraft();
        draft.goal = "x";                       // 有内容即可
        draft.allowedCapabilities = {};         // 没有能力声明也合法
        draft.userParameters = {};              // 没有用户参数也合法
        draft.candidateDigest.clear();          // 还没有候选也合法(恢复后才生成)
        CreationDraft restored;
        std::string error;
        Check(ParseCreationDraft(SerializeCreationDraft(draft), &restored, &error),
              "没有能力/参数/候选的草稿也合法");
        Check(restored.allowedCapabilities.empty(), "能力列表为空");
        Check(restored.userParameters.empty(), "用户参数为空");
        Check(restored.candidateDigest.empty(), "候选摘要为空");
        const auto plan = PlanDraftRestore(SerializeCreationDraft(draft), kSession);
        Check(plan.action == DraftRestoreAction::RestoreAndResume, "仍然可以接上继续");
    }

    std::printf("\nCCA-10 draft restore: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
