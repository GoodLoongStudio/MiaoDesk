// 共享的 JSON 字段读取器。
//
// 它被两个调用方共用:Pi 的 RPC 事件解析,和创作工具的 worker 参数解析。
// 分成两份的后果不是重复代码,是**两处可以不一致** —— 而不一致的地方恰好是
// creator_package_update 的 content:一个把 \n 解错、一个不解,写进包里的就是
// 另一份内容,而两边都以为自己对。
//
// 这里最重要的一条是"键位置":find 一个带引号的字段名会命中任何出现的地方,
// 包括**值**。"kind":"id" 里含有 "id",于是裸字符串查找会把"id 字段缺失"
// 判成"id 在"。校验器第一版就是这个 bug,而这个用例钉住它。
#include "miaodesk/JsonStringField.h"

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

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

void CheckEqD(const std::optional<double>& actual, double expected, double tolerance,
              const std::string& what) {
    ++g_checks;
    if (!actual.has_value() || !(std::fabs(*actual - expected) <= tolerance)) {
        ++g_failures;
        std::printf("FAIL  %s (期望 %.12g,实际 %s)\n", what.c_str(), expected,
                    actual.has_value() ? std::to_string(*actual).c_str() : "无值");
    }
}

} // namespace

int wmain() {
    using namespace miaodesk;

    // --- 基本读取 -------------------------------------------------------------
    CheckEq(ExtractJsonString(R"({"a":"1","b":"two"})", "\"b\""), "two", "取到第二个字段");
    CheckEq(ExtractJsonString("{\"a\":\"1\"}", "\"a\""), "1", "取到第一个字段");
    Check(ExtractJsonString("{\"a\":\"1\"}", "\"z\"").empty(), "没有的键返回空串");
    Check(ExtractJsonString("{\"a\":1}", "\"a\"").empty(), "值不是字符串时返回空串");

    // --- 转义 -----------------------------------------------------------------
    CheckEq(ExtractJsonString("{\"a\":\"x\\ny\"}", "\"a\""), "x\ny", "\\n 被解开");
    CheckEq(ExtractJsonString("{\"a\":\"x\\ty\"}", "\"a\""), "x\ty", "\\t 被解开");
    CheckEq(ExtractJsonString("{\"a\":\"x\\\"y\"}", "\"a\""), "x\"y",
            "值里带引号时不被提前截断");
    CheckEq(ExtractJsonString("{\"a\":\"x\\\\y\"}", "\"a\""), "x\\y", "反斜杠被解开");
    CheckEq(ExtractJsonString("{\"a\":\"a/b\"}", "\"a\""), "a/b", "\\/ 被解开");

    // --- 键位置 ---------------------------------------------------------------
    // 这一条是这份读解器存在的核心理由。
    Check(ExtractJsonString("{\"kind\":\"id\",\"name\":\"x\"}", "\"id\"").empty(),
          "值恰好等于字段名时,不能把它当成那个字段的值");
    Check(!JsonHasStringKey("{\"kind\":\"id\",\"name\":\"x\"}", "\"id\""),
          "值恰好等于字段名时,不算有这个字段");
    Check(JsonHasStringKey("{\"id\":\"a\"}", "\"id\""), "真正的字段被认出");
    // 键前面有空白与逗号都算键位置。
    Check(JsonHasStringKey("{ \"a\" : \"1\" }", "\"a\""), "键前面有空白仍然算");
    Check(JsonHasStringKey("{\"b\":\"1\",\"a\":\"2\"}", "\"a\""), "逗号之后的键算");
    // 嵌套对象里第二层出现的键:取第一个键位置的匹配。
    CheckEq(ExtractJsonString("{\"a\":\"outer\",\"o\":{\"a\":\"inner\"}}", "\"a\""), "outer",
            "同名键取外层那个(读解器只做扁平定位,这是已知边界)");

    // --- 整数 -----------------------------------------------------------------
    Check(ExtractJsonInt("{\"a\":1}", "\"a\"").has_value(), "整数取到");
    Check(*ExtractJsonInt("{\"a\":42}", "\"a\"") == 42, "且值对");
    Check(!ExtractJsonInt("{\"a\":\"1\"}", "\"a\"").has_value(), "字符串值不是整数");
    Check(!ExtractJsonInt("{\"a\":1}", "\"b\"").has_value(), "没有的键是 nullopt");
    Check(!ExtractJsonInt("{\"a\":abc}", "\"a\"").has_value(), "垃圾值是 nullopt");
    Check(!ExtractJsonInt("{\"a\":}", "\"a\"").has_value(), "空值是 nullopt");
    Check(!ExtractJsonInt("{\"a\":1x}", "\"a\"").has_value(), "尾部有垃圾也是 nullopt");
    // "没有 schema" 与 "schema 是 0" 必须能分开:0 恰好是个合法的旧版本号。
    Check(!ExtractJsonInt("{\"b\":1}", "\"schema\"").has_value(), "缺 schema 不是 0");
    Check(*ExtractJsonInt("{\"schema\":0}", "\"schema\"") == 0, "schema 是 0 就是 0");

    // ---- 字符串数组 ----
    // 为什么必须有这些用例:共用的 ExtractJsonStringArray 要的是**带引号的键**,
    // 而它替换掉的那一份本地实现自己补引号。两个调用点都因此一度传了裸键,于是
    // 一个返回"空 capabilities"(看起来像"没声明任何能力"),另一个返回 nullopt ——
    // 而这两种在调用方眼里都意味着"没有要检查的能力",虚构能力就此通过校验。
    const auto caps = ExtractJsonStringArray(R"({"capabilities":["clock.read","tasks.read"]})",
                                            "\"capabilities\"");
    Check(caps.has_value(), "带引号的键能取到数组");
    Check(caps && caps->size() == 2, "两个元素都在");
    Check(caps && (*caps)[0] == "clock.read", "按顺序读出,不排序不去重");
    Check(!ExtractJsonStringArray(R"({"capabilities":["clock.read"]})", "capabilities").has_value(),
          "裸键取不到 —— 这是刻意的:调用方必须带引号,否则值等于字段名时会误命中");
    const auto none = ExtractJsonStringArray(R"({"schema":1})", "\"capabilities\"");
    Check(!none.has_value(), "键不在是 nullopt,不是空数组");
    const auto empty = ExtractJsonStringArray(R"({"capabilities":[]})", "\"capabilities\"");
    Check(empty.has_value() && empty->empty(), "[] 是空数组 —— 与『键不在』是两件事");
    Check(!ExtractJsonStringArray(R"({"capabilities":"clock.read"})", "\"capabilities\"")
               .has_value(),
          "值是字符串不是数组,返回 nullopt 而不是把字符串当成一个元素");
    Check(!ExtractJsonStringArray(R"({"capabilities":[1,2]})", "\"capabilities\"").has_value(),
          "数组里有非字符串元素,返回 nullopt");
    Check(!ExtractJsonStringArray(R"({"capabilities":["a"})", "\"capabilities\"").has_value(),
          "没闭合的数组返回 nullopt");
    const auto escaped = ExtractJsonStringArray(
        R"({"capabilities":["a\"b","c\nd"]})", "\"capabilities\"");
    Check(escaped && escaped->size() == 2, "转义不影响元素个数");
    Check(escaped && (*escaped)[0] == "a\"b", "元素里的引号转义被解开");
    Check(escaped && (*escaped)[1] == "c\nd", "元素里的 \n 被解开(与 ExtractJsonString 同一套规则)");
    // 键位置:值恰好等于字段名时不能被当成键。
    Check(!ExtractJsonStringArray(R"({"x":"capabilities"})", "\"capabilities\"").has_value(),
          "值等于字段名不算有这个键");

    // ---- 小数取值:manifest 的几何字段全是小数,ExtractJsonInt 对 "0.30" 会停在 '.' ----
    std::printf("\n小数取值:\n");
    CheckEqD(ExtractJsonDouble(R"({"w":0.30})", "\"w\""), 0.30, 1e-12, "小数取到");
    CheckEqD(ExtractJsonDouble(R"({"w":1.0})", "\"w\""), 1.0, 1e-12, "1.0 这种写法也取到");
    CheckEqD(ExtractJsonDouble(R"({"w":2})", "\"w\""), 2.0, 1e-12, "整数按小数取");
    CheckEqD(ExtractJsonDouble(R"({"w":-1.5e2})", "\"w\""), -150.0, 1e-9, "科学计数法取到");
    CheckEqD(ExtractJsonDouble(R"({"w": 0.25 })", "\"w\""), 0.25, 1e-12, "冒号后有空白也取到");
    Check(!ExtractJsonDouble(R"({"w":"0.30"})", "\"w\"").has_value(), "字符串值不是小数");
    Check(!ExtractJsonDouble(R"({"w":abc})", "\"w\"").has_value(), "垃圾值是 nullopt");
    Check(!ExtractJsonDouble(R"({"w":1abc})", "\"w\"").has_value(),
          "1abc 取不到:数字后必须紧跟边界(写坏的字段该被拒,而不是被读成 1)");
    Check(!ExtractJsonDouble(R"({"w":0.30,"v":1})", "\"q\"").has_value(), "没有的键是 nullopt");
    CheckEqD(ExtractJsonDouble(R"({"w":0.30,"v":1})", "\"v\""), 1.0, 1e-12, "同文件里后一个键也取得到");
    Check(!ExtractJsonDouble(R"({"w":true})", "\"w\"").has_value(), "布尔值不是小数");
    Check(!ExtractJsonDouble(R"({"n":null})", "\"n\"").has_value(), "null 不是小数");
    // 键位置纪律与字符串取值一致:不能把值当成键。
    Check(!ExtractJsonDouble(R"({"x":"0.30"})", "\"0.30\"").has_value(), "值等于字段名不算有这个键");
    Check(!ExtractJsonDouble(R"({"kind":"w"})", "\"w\"").has_value(),
          "值里含字段名不算有这个键(manifest 里 kind 与 width 靠得太近时最要紧)");

    std::printf("\nJSON field reader: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
