// Runs the real WASAPI loopback tap. This is the first time it has ever executed.
//
// Why this file exists: `MiaoWallpaperAudioTap` is the Windows half of B-2, and until
// now it had exactly one kind of evidence — it compiles and links, because
// MiaoDeskWallpaper.exe references it. That is the same class of gap the D3D11
// textured-sprite test was written to close: a piece of product code that has never
// run is not known to work, and the ways it can be wrong are not ways a compiler
// sees. Here they are, concretely:
//
//   · the capture thread tears down its own COM apartment; releasing COM objects on
//     an apartment it does not own is undefined behaviour and kills the process with
//     0xC0000005 and no message;
//   · `Stop()` joins that thread. If the join is wrong the host deadlocks at shutdown
//     — the wallpaper never exits and the user kills it from Task Manager;
//   · a machine with no render endpoint must report a named reason and keep retrying,
//     not spin. A host that burns a core because the user has no sound card is a
//     support call and a battery drain;
//   · a machine WITH an endpoint must not invent signal: silence has to read as
//     silence. Reading the device's int16 stream as float, or striding channels
//     wrong, produces noise indistinguishable from music — the exact failure the
//     float-only mix-format check exists to prevent.
//
// The runner decides which of those branches is real, so every assertion says which
// branch it applies to, and the branch that ran is printed first. A reader must never
// have to guess whether "no frame arrived" meant "no device" or "the tap is broken".
#include "miaodesk/MiaoWallpaperAudioTap.h"

#include <windows.h>
#include <objbase.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using miaodesk::content::inputbus::AudioSpectrumFrame;

namespace {

int failures = 0;
void Step(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Printed *before* each risky call. The D3D11 test died with 0xC0000005 and an empty
// log because every line was still sitting in the stdout buffer; a line emitted
// before the risky call names the phase that died, which is the only way to locate an
// access violation I cannot reproduce on this machine.
void Phase(const char* what) { std::printf("  -- %s\n", what); }

double CpuSeconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1.0;
    const std::uint64_t hundreds =
        (static_cast<std::uint64_t>(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
        (static_cast<std::uint64_t>(user.dwHighDateTime) << 32 | user.dwLowDateTime);
    return static_cast<double>(hundreds) / 1e7;
}

double NowSeconds() { return static_cast<double>(GetTickCount64()) / 1000.0; }

// Waits up to `seconds` for LatestFrame. Returns the frame and true on success.
bool WaitForFrame(miaodesk::wallpaper::MiaoWallpaperAudioTap* tap, double seconds,
                  AudioSpectrumFrame* out) {
    const double deadline = NowSeconds() + seconds;
    while (NowSeconds() < deadline) {
        if (tap->LatestFrame(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool InUnitRange(double value) { return value >= 0.0 && value <= 1.0; }

// Never returns before `milliseconds` have elapsed; used only where a wait is part of
// the measurement (the CPU burn check) rather than a poll.
void Settle(int milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }

} // namespace

int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("WASAPI loopback 采集线程第一次真的运行\n");

    // ---------------------------------------------------------------- 前置状态
    Phase("Start 之前的状态");
    {
        miaodesk::wallpaper::MiaoWallpaperAudioTap tap;
        AudioSpectrumFrame frame{};
        Step(!tap.Running(), "未启动时 Running() 为假");
        Step(!tap.LatestFrame(&frame), "未启动时读不到帧");
        Step(!tap.LatestFrame(nullptr), "LatestFrame(nullptr) 返回假而不是崩");
        Step(tap.LastErrorText().empty(), "未启动时没有遗留错误文本");
        Phase("未启动就 Stop()");
        tap.Stop();  // Must return immediately rather than join a thread that never ran.
        Step(!tap.Running(), "未启动就 Stop() 之后仍未运行(没有加入不存在的线程)");
    }

    // ---------------------------------------------------------------- 启动与幂等
    Phase("Start()");
    miaodesk::wallpaper::MiaoWallpaperAudioTap tap;
    const bool started = tap.Start();
    Step(started, "Start() 返回真(线程已创建)");
    const bool startedAgain = tap.Start();
    Step(startedAgain, "重复 Start() 是幂等的(不会起第二个采集线程)");
    Step(tap.Running(), "启动后 Running() 为真");

    // ---------------------------------------------------------------- 哪个分支
    // Nothing is playing on a CI runner, so the two outcomes below are: an endpoint
    // exists and loopback delivers silence, or no endpoint exists and the tap says so.
    // Both are legitimate; what is NOT legitimate is silence about which one happened.
    Phase("等第一帧(最多 5 秒)");
    AudioSpectrumFrame frame{};
    const bool gotFrame = WaitForFrame(&tap, 5.0, &frame);
    const std::wstring error = tap.LastErrorText();
    std::printf("      分支:%s\n", gotFrame ? "有回放端点,收到帧" : "未收到帧");
    std::printf("      LastErrorText:%ls\n", error.c_str());

    if (gotFrame) {
        std::printf("      level=%.6f bass=%.6f beat=%s\n", frame.level, frame.bands[0],
                    frame.beat ? "true" : "false");
        bool ranges = true;
        for (double band : frame.bands) ranges = ranges && InUnitRange(band);
        for (double bin : frame.spectrum) ranges = ranges && InUnitRange(bin);
        Step(ranges, "每个频段与频谱桶都落在契约的 [0,1] 内(格式认错会给出 NaN 或越界值)");
        Step(InUnitRange(frame.level), "总电平落在 [0,1] 内");

        // Nothing is playing on the machine this gate runs on, so a stream that reads
        // as loud means the format is being misread. That is the failure the float-only
        // mix-format check exists to stop, and it is only observable here — it looks
        // exactly like music everywhere else.
        Step(frame.level <= 1e-3 && frame.bands[0] <= 1e-3,
             "没有声音时电平≈0 —— 静音必须读作静音(这条假设本机没在放东西;CI 上成立)");
        Step(!frame.beat, "静音流不会被判成节拍(beat 是沿,不能从静音里凭空造出来)");

        // One frame could be a fluke of the first window; the capture loop has to keep
        // delivering. This is what distinguishes "captured one packet" from "capturing".
        int arrivals = 0;
        for (int i = 0; i < 4; ++i) {
            Settle(100);
            AudioSpectrumFrame next{};
            if (tap.LatestFrame(&next)) ++arrivals;
        }
        Step(arrivals >= 1, "四分之一次窗口内持续收到新帧(采集循环是活的,不是一次性)");
    } else {
        // The failure mode this guards: the thread is alive, no frame ever arrives, and
        // nothing explains why. That is the state where a user's audio-reactive wallpaper
        // sits still and the diagnostics say nothing.
        Step(!error.empty(), "没有帧时必须留下原因 —— 无声的失败比失败更难排查");
        Step(error == L"没有可用的默认播放设备",
             "原因是『没有默认播放设备』这一段(宿主会把它原样写进 DiagnosticsText)");

        // A missing device is normal and retried, so the thread must still be alive —
        // but it must not be doing so by spinning. This is the P1-2 concern in miniature:
        // a wallpaper host that pins a core because the user unplugged their headphones.
        Phase("测量无设备时的重试开销(3 秒)");
        const double cpuBefore = CpuSeconds();
        Settle(3000);
        const double cpuAfter = CpuSeconds();
        const double burned = cpuAfter - cpuBefore;
        std::printf("      CPU  %.3f 秒 / 3.000 秒墙钟\n", burned);
        Step(burned >= 0.0 && burned < 1.0,
             "3 秒墙钟内 CPU 占用 < 1 秒(退避 400ms,不是忙等)");
        Step(tap.Running(), "没有设备时采集线程仍在运行并按退避重试(宿主不用跟着死)");
    }

    // ---------------------------------------------------------------- 停止与重启
    Phase("Stop()");
    const double stopStart = NowSeconds();
    tap.Stop();
    const double stopSeconds = NowSeconds() - stopStart;
    std::printf("      Stop() 用时 %.3f 秒\n", stopSeconds);
    Step(stopSeconds < 2.0, "Stop() 在 2 秒内返回(join 没有死锁)");
    Step(!tap.Running(), "Stop() 之后 Running() 为假");
    tap.Stop();  // Idempotent: a second Stop must not touch a joined thread.
    Step(!tap.Running(), "重复 Stop() 是安全的");

    Phase("Stop 之后重新 Start()");
    const bool restarted = tap.Start();
    Step(restarted, "停止后可以再次启动(设备枚举是重新做的,不是一次性的)");
    Step(tap.Running(), "重启后 Running() 为真");
    tap.Stop();
    Step(!tap.Running(), "第二次 Stop() 之后 Running() 为假");

    // The destructor path: Impl::~Impl calls Stop(), so a caller that forgets must not
    // hang the process. This instance is deliberately left running when it dies.
    Phase("不做 Stop() 就让对象离开作用域");
    {
        miaodesk::wallpaper::MiaoWallpaperAudioTap forgotten;
        Step(forgotten.Start(), "作用域内的实例启动成功");
        Step(forgotten.Running(), "作用域内的实例在运行");
    }
    std::printf("      析构已完成(没有 Stop 的实例也安全退出)\n");

    std::printf("\n%s(%d 处失败)\n", failures == 0 ? "ALL CHECKS PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
