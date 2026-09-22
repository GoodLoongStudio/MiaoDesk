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

#include <cstdio>
#include <string>

using miaodesk::wallpaper::BuildAudioBridgeEnvelope;
using miaodesk::content::inputbus::AudioSpectrumFrame;

static void Emit(const AudioSpectrumFrame& frame) {
    std::printf("{\"level\":%.17g,\"bands\":[", frame.level);
    for (std::size_t i = 0; i < frame.bands.size(); ++i) {
        std::printf("%s%.17g", i == 0 ? "" : ",", frame.bands[i]);
    }
    std::printf("],\"spectrum\":[");
    for (std::size_t i = 0; i < frame.spectrum.size(); ++i) {
        std::printf("%s%.17g", i == 0 ? "" : ",", frame.spectrum[i]);
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

    // 3. 需要被钳的输入:上溢、下溢、NaN。NaN 不能写进字面量输入行(node 那边读不到),
    //    所以输入行写 null 表示"这一位故意不是数",由 node 侧按"不参与逐位比对"处理。
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

    return 0;
}
