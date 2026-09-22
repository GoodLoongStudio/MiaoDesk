// 验证 BuildAudioBridgeEnvelope 产出的信封,正是注入的 shim 会接受的那一个。
//
// 为什么值得单独立一个测试:B-6 的契约两头各有一份实现 —— 宿主侧这个信封构造器,
// 页面侧 WallpaperWebAudioBridge.js 的 normalizeFrame()。两头之间没有任何编译器在
// 检查它们,只有"字段名、数组长度、值域"这三件事对不上就整帧被丢弃,而页面侧丢弃是
// 静默的(normalizeFrame 返回 null,onMessage 直接 return)。也就是说这条链一旦漂移,
// 表现是"壁纸收不到音频",不带任何错误信息。
//
// 页面侧的 shim 由 node 测试 tests/WebAudioBridge.mjs 覆盖;那个测试读的是同一份字节,
// 因此两边都在被测。这里专注宿主侧,并且刻意验三件容易错的事:
//   · 数组长度:shim 用 numericArray() 按长度拒绝,长度错 = 全丢
//   · beat 必须是 JSON 布尔:shim 用 === true 判沿,发 1 会每帧都成节拍
//   · 小数点:ostringstream 跟全局 locale 走,逗号小数点的机器上 JSON.parse 会抛
#include "miaodesk/WallpaperWebAudioEnvelope.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <locale>
#include <string>

using miaodesk::wallpaper::BuildAudioBridgeEnvelope;
using miaodesk::content::inputbus::AudioSpectrumFrame;

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static std::size_t Count(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    std::size_t at = text.find(needle);
    while (at != std::string::npos) {
        ++n;
        at = text.find(needle, at + needle.size());
    }
    return n;
}

// 取 "name":[ ... ] 的数组内容段。两端都定位 —— 上一版从开头 substr 到字符串末尾,
// 于是把 `],"beat"` 之间那个分隔逗号也数了进去,bands 与 spectrum 双双多数一个。
// 信封本身是对的,是我的切片错了。
static std::string ArrayBody(const std::string& json, const char* name) {
    const std::string open = std::string("\"") + name + "\":[";
    const std::size_t at = json.find(open);
    if (at == std::string::npos) return {};
    const std::size_t first = at + open.size();
    const std::size_t close = json.find(']', first);
    if (close == std::string::npos) return {};
    return json.substr(first, close - first);
}

static AudioSpectrumFrame FlatFrame() {
    AudioSpectrumFrame frame;
    frame.level = 0.5;
    frame.bands = {0.1, 0.2, 0.3, 0.4, 0.5};
    for (auto& bin : frame.spectrum) bin = 0.25;
    frame.beat = false;
    return frame;
}

static void TestShape() {
    std::printf("\n1. 信封形状(与 normalizeFrame 的接受条件逐条对齐)\n");
    const std::string json = BuildAudioBridgeEnvelope(FlatFrame());

    Check(json.rfind("{\"type\":\"audio\",\"frame\":{", 0) == 0, "以 type:audio + frame 对象开头");
    Check(json.substr(json.size() - 2) == "}}", "以两个右花括号收尾(没有多余字段被夹在中间)");
    Check(json.find("\"level\":") != std::string::npos, "有 level 字段");
    Check(json.find("\"bands\":[") != std::string::npos, "有 bands 数组");
    Check(json.find("\"spectrum\":[") != std::string::npos, "有 spectrum 数组");
    Check(json.find("\"beat\":") != std::string::npos, "有 beat 字段");

    // numericArray() 的判据是 length 精确等于期望值,少一个多一个都整帧丢弃。
    const std::string bands = ArrayBody(json, "bands");
    const std::string spectrum = ArrayBody(json, "spectrum");
    Check(!bands.empty() && Count(bands, ",") == 4, "bands 里 5 个数(4 个逗号)");
    Check(!spectrum.empty() && Count(spectrum, ",") == 15, "spectrum 里 16 个数(15 个逗号)");

    Check(json.find(",,") == std::string::npos, "没有空数组元素(那会让 JSON.parse 抛)");
    Check(json.find("[]") == std::string::npos, "没有空数组");
    // 尾随逗号同样不是合法 JSON。这两个形状在"列表是空的/最后一个值是钳出来的"
    // 时候最容易出现,而它们只会让整页收不到音频。
    Check(json.find(",]") == std::string::npos && json.find(",}") == std::string::npos,
          "没有尾随逗号(,] 或 ,} 都不是合法 JSON)");
    Check(json.find("[,") == std::string::npos, "没有前置逗号");
}

static void TestBeatIsBoolean() {
    std::printf("\n2. beat 是 JSON 布尔,不是 0/1\n");
    AudioSpectrumFrame frame = FlatFrame();
    frame.beat = true;
    Check(BuildAudioBridgeEnvelope(frame).find("\"beat\":true") != std::string::npos,
          "beat=true 发 true(shim 用 === true 判沿)");
    frame.beat = false;
    Check(BuildAudioBridgeEnvelope(frame).find("\"beat\":false") != std::string::npos,
          "beat=false 发 false");
    Check(BuildAudioBridgeEnvelope(frame).find("\"beat\":0") == std::string::npos,
          "不发 0 —— 那样每帧都会被读成节拍");
}

static void TestValueCoercion() {
    std::printf("\n3. 值域与有限性(镜像 shim 的 unit())\n");
    AudioSpectrumFrame frame = FlatFrame();

    frame.level = 0.123456789;
    Check(BuildAudioBridgeEnvelope(frame).find("\"level\":0.1235") != std::string::npos,
          "level 保留四位小数(0.123456789 -> 0.1235)");
    frame.level = 0.00001;
    Check(BuildAudioBridgeEnvelope(frame).find("\"level\":0.0000") != std::string::npos,
          "小于一位小数的值不出科学计数法(1e-05 -> 0.0000)");

    frame = FlatFrame();
    frame.bands = {2.0, -1.0, std::nan(""), 0.5, 1.0};
    const std::string json = BuildAudioBridgeEnvelope(frame);
    Check(ArrayBody(json, "bands") == "1.0000,0.0000,0.0000,0.5000,1.0000",
          "上溢钳到 1、下溢与 NaN 钳到 0(与 shim 的 unit() 同一结果)");
    Check(json.find("nan") == std::string::npos && json.find("inf") == std::string::npos,
          "不产出 nan/inf 字面量 —— 那不是合法 JSON");

    frame = FlatFrame();
    frame.level = std::numeric_limits<double>::infinity();
    Check(BuildAudioBridgeEnvelope(frame).find("\"level\":1.0000") != std::string::npos,
          "level=inf 钳到 1 而不是输出 inf");
    frame.level = -std::numeric_limits<double>::quiet_NaN();
    Check(BuildAudioBridgeEnvelope(frame).find("\"level\":0.0000") != std::string::npos,
          "level=-NaN 钳到 0");

    // 负零。第一版 unit() 写的是 `!(value >= 0.0)`,于是 -0.0 被判为在区间内、
    // 一路走到格式化,产出 "level":-0.0000 —— 合法 JSON,但是个疙瘩,而且比疙瘩更糟的
    // 是不对称:-1e-300 会被钳成 0.0000,-0.0 却不会。
    frame.level = -0.0;
    const std::string negativeZero = BuildAudioBridgeEnvelope(frame);
    Check(negativeZero.find("-0.0000") == std::string::npos && negativeZero.find("\"level\":0.0000") != std::string::npos,
          "level=-0.0 输出 0.0000(不输出 -0.0000)");

    // 次正规数与极小额:固定小数位下必须落成 0.0000,不能变成科学计数法。
    frame.level = std::numeric_limits<double>::denorm_min();
    Check(BuildAudioBridgeEnvelope(frame).find("e-") == std::string::npos &&
              BuildAudioBridgeEnvelope(frame).find("\"level\":0.0000") != std::string::npos,
          "denorm_min 落成 0.0000(不出科学计数法)");
}

static void TestLocaleIndependence() {
    std::printf("\n4. 小数点与 locale 无关\n");
    AudioSpectrumFrame frame = FlatFrame();
    const std::string pinned = BuildAudioBridgeEnvelope(frame);

    // 不能简单查"没有逗号" —— JSON 本身用逗号分隔。该查的是"两个数字之间没有逗号":
    // 小数逗号一定会以 数字,数字 的形态出现。
    Check(pinned.find("0,5") == std::string::npos && pinned.find("1,0") == std::string::npos,
          "没有把逗号当小数点用(JSON 结构逗号只出现在 ] , \" 这类边界上)");

    // 直接验 imbue 那一行有用:把全局 locale 的十进制点换成 ',' 再构造一次。
    // 若函数内部没 imbue classic,这里两个字符串就会不同。
    struct CommaPoint : std::numpunct<char> {
        char do_decimal_point() const override { return ','; }
    };
    const std::locale previous = std::locale::global(std::locale::classic());
    std::locale::global(std::locale(previous, new CommaPoint));
    const std::string underCommaLocale = BuildAudioBridgeEnvelope(frame);
    std::locale::global(previous);
    Check(pinned == underCommaLocale,
          "全局 locale 的十进制点改成 ',' 之后输出逐字节不变");
    Check(underCommaLocale.find("0,5") == std::string::npos,
          "逗号小数点的机器上也不产出 0,5(那会让页面 JSON.parse 抛)");
}

static void TestDeterminism() {
    std::printf("\n5. 可复现性\n");
    AudioSpectrumFrame frame = FlatFrame();
    const std::string first = BuildAudioBridgeEnvelope(frame);
    bool stable = true;
    for (int i = 0; i < 64; ++i) {
        if (BuildAudioBridgeEnvelope(frame) != first) stable = false;
    }
    Check(stable, "同一帧连续构造 64 次结果一致(没有迭代器/内存地址漏进输出)");

    Check(first.find("0.5000") != std::string::npos, "0.5 输出为 0.5000(定点,不是 0.5)");
}

// wmain 而不是 main:scripts/run-pure-logic-tests.sh 用一个共享的 wmain_shim.cpp
// 作入口(与 SceneSerializerSelfTest.cpp / ContentSelfTests.cpp 同一约定)。
// 写成 main 会在链接期报 "Undefined symbols: wmain()" —— 我第一次就是这么错的。
int wmain() {
    std::printf("WallpaperWebAudioEnvelope:宿主->页面的音频信封构造\n");
    TestShape();
    TestBeatIsBoolean();
    TestValueCoercion();
    TestLocaleIndependence();
    TestDeterminism();
    std::printf("\n%s(%d 处失败)\n", failures == 0 ? "ALL CHECKS PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
