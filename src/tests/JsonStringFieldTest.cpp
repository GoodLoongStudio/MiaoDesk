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

#include <cstdio>
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

    std::printf("\nJSON field reader: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        std::printf("ALL CHECKS FAILED\n");
        return 1;
    }
    std::printf("ALL CHECKS PASSED\n");
    return 0;
}
