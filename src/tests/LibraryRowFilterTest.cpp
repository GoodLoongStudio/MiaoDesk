// P0-07「库状态一致恢复」:被跳过的库行必须说得出口。
//
// `WallpaperLibrary::Load:283` 原本在"ID 空"或"Kind 不认识"时静默丢行,而 Load
// 照常返回 true,头文件上也没有"跳过几行"的出口 —— 于是调用方只问成败,拿着一个
// 悄悄变短的库继续。这个文件钉住判定与那句话。
//
// 反空洞自检恒返回 None 的函数在其余断言上同样全绿,所以先喂一个明知该跳的
// (不认识的 Kind)和一个明知不该跳的(合法小写),确认判定真的会动。
#include "miaodesk/MiaoLibraryRowFilter.h"

#include <cstdio>
#include <string>

namespace miaodesk::library_row {
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

bool VerdictStillMoves() {
    return ClassifyLibraryRow(L"wall-1", L"hologram") == RowSkipReason::UnknownKind &&
           ClassifyLibraryRow(L"wall-1", L"image") == RowSkipReason::None;
}

} // namespace
} // namespace miaodesk::library_row

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::library_row;

    if (!VerdictStillMoves()) {
        std::printf("\n[FAIL] 反空洞自检没通过:判定恒返回同一答案\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:不认识的 Kind 被判跳过、合法的被判不跳过\n");
    ++g_checks;

    // ---- 1. 四种字表都认,且大小写/空白不敏感 ----
    // 空白敏感是有意的:INI 是手可改的," scene" 与 "scene" 必须是同一档。
    // 不 Trim 的话,一个手拍上去的空格就会让用户的壁纸从库里消失。
    {
        Check(IsKnownLibraryKind(L"image"), "认识 image");
        Check(IsKnownLibraryKind(L"video"), "认识 video");
        Check(IsKnownLibraryKind(L"web"), "认识 web");
        Check(IsKnownLibraryKind(L"scene"), "认识 scene");
        Check(IsKnownLibraryKind(L"IMAGE"), "大写也算(ParseKind 就是大小写不敏感的)");
        Check(IsKnownLibraryKind(L" Scene "), "两端空白也算 —— 手改 INI 多个空格不该让壁纸消失");
        Check(!IsKnownLibraryKind(L"hologram"), "不认识的字表外词");
        Check(!IsKnownLibraryKind(L""), "空串不认识");
        Check(!IsKnownLibraryKind(L"   "), "全空白不认识");
    }

    // ---- 2. 三种跳过理由必须分开 ----
    // "损坏"与"版本差异"对用户是两回事:前者该修配置,后者该升级或改配置。
    {
        Check(ClassifyLibraryRow(L"", L"image") == RowSkipReason::MissingId, "ID 空判 MissingId");
        Check(ClassifyLibraryRow(L"", L"hologram") == RowSkipReason::MissingId,
              "ID 空时先报 ID —— 没有 ID 连它是什么都指不出来");
        Check(ClassifyLibraryRow(L"w1", L"") == RowSkipReason::MissingKind, "Kind 空判 MissingKind");
        Check(ClassifyLibraryRow(L"w1", L"   ") == RowSkipReason::MissingKind, "Kind 全空白判 MissingKind");
        Check(ClassifyLibraryRow(L"w1", L"hologram") == RowSkipReason::UnknownKind,
              "Kind 写了不认识的词判 UnknownKind");
        Check(ClassifyLibraryRow(L"w1", L"scene") == RowSkipReason::None, "合法行不跳过");
    }

    // ---- 3. 那句话要说清三件事 ----
    {
        const std::wstring unknown = DescribeRowSkip(L"wall-7", L"hologram", RowSkipReason::UnknownKind);
        Check(unknown.find(L"wall-7") != std::wstring::npos, "点出是哪一条");
        Check(unknown.find(L"hologram") != std::wstring::npos, "点出是哪个词");
        Check(unknown.find(L"image/video/web/scene") != std::wstring::npos,
              "点出本版本认识哪些 —— 用户才知道该升级还是该改配置");
        Check(unknown.find(L"版本") != std::wstring::npos, "说清这可能是版本差异");

        const std::wstring missingKind = DescribeRowSkip(L"wall-8", L"", RowSkipReason::MissingKind);
        Check(missingKind.find(L"wall-8") != std::wstring::npos, "缺 Kind 也点出是哪一条");
        Check(missingKind.find(L"损坏") != std::wstring::npos, "且说清这是损坏,不是版本差异");

        const std::wstring missingId = DescribeRowSkip(L"", L"image", RowSkipReason::MissingId);
        Check(missingId.find(L"损坏") != std::wstring::npos, "没有 ID 也说清是损坏");
        Check(!missingId.empty(), "没有 ID 时也有话可说");

        Check(DescribeRowSkip(L"w", L"scene", RowSkipReason::None).empty(), "没跳过就没有话");
    }

    // ---- 4. 名字都不是空的 ----
    {
        for (const auto reason : {RowSkipReason::None, RowSkipReason::MissingId,
                                  RowSkipReason::MissingKind, RowSkipReason::UnknownKind}) {
            Check(RowSkipReasonName(reason) != nullptr && RowSkipReasonName(reason)[0] != L'\0',
                  "每个理由都有非空名字(日志要能印出来)");
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n库行跳过判定:全部 %d 项通过\n", g_checks);
    return 0;
}
