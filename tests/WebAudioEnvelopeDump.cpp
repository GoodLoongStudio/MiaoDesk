// 打印 BuildAudioBridgeEnvelope 的真实输出,供 node 侧拿去过一遍真 shim。
//
// 为什么要多这一个可执行:B-6 的契约有两份实现、没有一个编译器在检查它们之间的关系 ——
// 宿主侧这个信封构造器,页面侧 WallpaperWebAudioBridge.js 的 normalizeFrame()。
// 两边各自的单测都绿,不代表这两份字节能对上;而对不上的表现是"页面什么都收不到",
// 且没有任何错误信息。
//
// 所以这里把 C++ 的真实产出交给 node 去跑真的 shim。输出约定(一行输入、一行信封,交替):
//   第 2k 行   构造这一帧用的输入,JSON
//   第 2k+1 行 BuildAudioBridgeEnvelope 对该帧的输出
// 分成两行而不是把信封塞进 JSON 字符串,是为了避免在 C++ 里手写 JSON 转义 ——
// 那正是这份代码最可能出错的地方,而它本身不该被测。
#include "miaodesk/WallpaperWebAudioEnvelope.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

using miaodesk::wallpaper::BuildAudioBridgeEnvelope;
using miaodesk::content::inputbus::AudioSpectrumFrame;

static void Emit(const AudioSpectrumFrame& frame) {
    // 非有限值写成 "nan" / "inf" / "-inf" 三个字符串,而不是一律 null。
    // 第一版用 null,node 侧立刻抓到问题:三种非有限钳出来的结果**不一样** ——
    // nan 和 -inf 是 0,+inf 是 1。一个 null Marker 分辨不出该期望哪个。
    // (另外 %.17g 会把 NaN 印成 "nan"/"-nan",那不是合法 JSON,node 的 JSON.parse 会抛。)
    auto number = [](double value) {
        if (value != value) return std::string("\"nan\"");
        if (value == HUGE_VAL) return std::string("\"inf\"");
        if (value == -HUGE_VAL) return std::string("\"-inf\"");
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.17g", value);
        return std::string(buffer);
    };

    std::printf("{\"level\":%s,\"bands\":[", number(frame.level).c_str());
    for (std::size_t i = 0; i < frame.bands.size(); ++i) {
        std::printf("%s%s", i == 0 ? "" : ",", number(frame.bands[i]).c_str());
    }
    std::printf("],\"spectrum\":[");
    for (std::size_t i = 0; i < frame.spectrum.size(); ++i) {
        std::printf("%s%s", i == 0 ? "" : ",", number(frame.spectrum[i]).c_str());
    }
    std::printf("],\"beat\":%s}\n", frame.beat ? "true" : "false");

    const std::string envelope = BuildAudioBridgeEnvelope(frame);
    std::printf("%s\n", envelope.c_str());
}

int main() {
    AudioSpectrumFrame frame;

    // 1. 常帧:每个值都在 [0,1] 且互不相同,便于逐位比对。
    frame.level = 0.5;
    frame.bands = {0.1, 0.25, 0.5, 0.75, 1.0};
    for (std::size_t i = 0; i < frame.spectrum.size(); ++i) {
        frame.spectrum[i] = static_cast<double>(i) / static_cast<double>(frame.spectrum.size() - 1);
    }
    frame.beat = false;
    Emit(frame);

    // 2. beat 为真。
    frame.beat = true;
    Emit(frame);

    // 3. 需要被钳的输入:上溢与下溢。
    frame.beat = false;
    frame.level = 0.0;
    frame.bands = {0.0, 0.5, 0.0, 0.0, 0.0};
    for (auto& bin : frame.spectrum) bin = 0.0;
    Emit(frame);

    // 4. 边界极值。
    frame.level = 1.0;
    frame.bands = {1.0, 1.0, 1.0, 1.0, 1.0};
    for (auto& bin : frame.spectrum) bin = 1.0;
    Emit(frame);

    // 5. 非有限值。这一条是前四条都没覆盖的:信封构造器必须把 NaN / inf / -inf
    //    钳成 0 / 1 / 0,而 shim 的 unit() 会各自再钳一次。两边的钳位必须一致,
    //    否则"分析器出了一个 NaN"会以静默错误幅度的形式抵达页面。
    frame.level = std::numeric_limits<double>::quiet_NaN();
    frame.bands = {2.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                   std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity()};
    for (std::size_t i = 0; i < frame.spectrum.size(); ++i) {
        frame.spectrum[i] = (i % 3 == 0) ? 0.5
                          : (i % 3 == 1) ? std::numeric_limits<double>::quiet_NaN()
                                         : -1.0;   // 下溢,钳到 0
    }
    Emit(frame);

    return 0;
}
