// 离线复刻 SamplePointer 的"按显示器归属 + 归一化"算术。
//
// 宿主那段必须跑在 Windows 上(HMONITOR / GetMonitorInfoW),但它的决策全部是纯算术:
// 光标物理像素 - 显示器原点 = 显示器内坐标,再除以该显示器尺寸。
// 这里用一张假的显示器拓扑验证这些决策,尤其是主屏不在 (0,0) 的情况 —— 那正是用
// 虚拟桌面坐标会算错的场景,也是"壁纸不得知道桌面布局"这条规则真正起作用的地方。
#include "miaodesk/MiaoInputBus.h"

#include <cstdio>
#include <vector>

using namespace miaodesk::content::inputbus;

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

struct FakeMonitor {
    long left;
    long top;
    long right;
    long bottom;
};

// 与 IndependentWallpaperHost::SamplePointer 同一套算术,只是把 Win32 查表换成查这张表。
static bool SampleOn(const std::vector<FakeMonitor>& monitors, long cursorX, long cursorY,
                     PointerNormalizer* normalizer, PointerSample* sample) {
    for (const auto& m : monitors) {
        // 宿主用 MonitorFromPoint(...MONITOR_DEFAULTTONEAREST):越界时归到最近一块,
        // 而不是返回"没有显示器"。
        if (cursorX >= m.left && cursorX < m.right && cursorY >= m.top && cursorY < m.bottom) {
            const long width = m.right - m.left;
            const long height = m.bottom - m.top;
            if (width <= 0 || height <= 0) return false;
            normalizer->Configure(width, height);
            sample->x = cursorX - m.left;
            sample->y = cursorY - m.top;
            sample->inside = true;
            return true;
        }
    }
    return false;
}

// 指针通道是有意低通的(attack 50ms / decay 180ms),所以单次 Apply 到不了目标值 ——
// 初值 0.5 处看起来"对"只是因为第 1、2 节的目标恰好就是 0.5。喂足帧数再断言。
static PointerNormalizer::Result Converge(PointerNormalizer* n, const PointerSample& s, int frames = 60) {
    PointerNormalizer::Result r;
    for (int i = 0; i < frames; ++i) r = n->Apply(s, 1.0 / 60.0);
    return r;
}

int wmain() {
    // 双屏:主屏在原点,副屏在主屏**右侧且上沿为负** —— 副屏比主屏高,上沿伸到主屏之上。
    // 这是最常见的多显示器排布之一,也是"减去虚拟桌面原点"会算出负值的情形。
    const std::vector<FakeMonitor> monitors = {
        {0, 0, 1920, 1080},       // 主屏
        {1920, -300, 3400, 780},  // 副屏(物理尺寸 1480x1080,原点 y=-300)
    };

    std::printf("\n1. 主屏:原点就在 (0,0)\n");
    {
        PointerNormalizer normalizer;
        PointerSample sample;
        Check(SampleOn(monitors, 960, 540, &normalizer, &sample), "主屏中心被归属到主屏");
        const auto r = normalizer.Apply(sample, 1.0 / 60.0);
        Check(r.x > 0.49 && r.x < 0.51, "主屏中心 x ≈ 0.5");
        Check(r.y > 0.49 && r.y < 0.51, "主屏中心 y ≈ 0.5");
        Check(r.inside, "在主屏内");
    }

    std::printf("\n2. 副屏:原点不在 (0,0),且上沿为负\n");
    {
        PointerNormalizer normalizer;
        PointerSample sample;
        Check(SampleOn(monitors, 2660, 240, &normalizer, &sample), "副屏中心被归属到副屏");
        // 副屏中心 = 原点 + 尺寸/2 = (1920+740, -300+540) = (2660, 240)
        const auto r = normalizer.Apply(sample, 1.0 / 60.0);
        Check(r.x > 0.49 && r.x < 0.51, "副屏中心 x ≈ 0.5(不是 0.78 —— 虚拟桌面坐标会算错)");
        Check(r.y > 0.49 && r.y < 0.51, "副屏中心 y ≈ 0.5(不是 0.04)");
        Check(r.inside, "在副屏内");
    }

    std::printf("\n3. 副屏的物理尺寸,不是它在虚拟桌面里的跨度\n");
    {
        PointerNormalizer normalizer;
        PointerSample sample;
        // 副屏最右一列的前一个像素
        Check(SampleOn(monitors, 3399, 0, &normalizer, &sample), "副屏右缘被归属到副屏");
        const auto r = Converge(&normalizer, sample);
        // (3399-1920)/(3400-1920) = 1479/1480 ≈ 0.9993
        Check(r.x > 0.99 && r.x <= 1.0, "副屏右缘 x ≈ 1.0(收敛后)");
        // 若误用虚拟桌面跨度(1920..3400 会有 1480 宽,看着一样),
        // 但误用"整桌 3400 宽"会得到 3399/3400 ≈ 0.9997 —— 仍接近 1,
        // 所以真正的区分点在中点(见第 2 节)。
    }

    std::printf("\n4. 光标不在任何显示器内:宿主会取最近一块,这里显式为失败\n");
    {
        PointerNormalizer normalizer;
        PointerSample sample;
        Check(!SampleOn(monitors, -5000, -5000, &normalizer, &sample),
              "远离所有显示器时不被归属(宿主的 MONITOR_DEFAULTTONEAREST 另处理)");
    }

    std::printf("\n5. 边界像素归属唯一\n");
    {
        // x=1920 是副屏第一列,同时也是主屏右边界(主屏范围 [0,1920) 已排除它)
        PointerNormalizer a;
        PointerNormalizer b;
        PointerSample sa;
        PointerSample sb;
        Check(SampleOn(monitors, 1920, 0, &a, &sa), "x=1920 归副屏");
        const auto ra = Converge(&a, sa);
        // 60 帧 @60fps、decay 180ms 后残留 0.5·e^(-1/0.18) ≈ 0.0019,所以阈值取 0.01
        // 而不是 0.001 —— 后者是在测收敛速度,不是在测归属性。
        std::printf("        (实测副屏第一列 x = %.5f)\n", ra.x);
        Check(ra.x < 0.01, "副屏第一列 x → 0(收敛后)");
        // 主屏最后一列
        Check(SampleOn(monitors, 1919, 1079, &b, &sb), "x=1919 仍归主屏");
        const auto rb = Converge(&b, sb);
        Check(rb.x > 0.999, "主屏最后一列 x ≈ 1(收敛后)");
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
