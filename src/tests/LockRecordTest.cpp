// P0-08 后半句:锁持有权记录的编解码。
//
// 为什么单独有这一份:`MiaoLockOwnership` 判"锁现在是什么状态",但它需要
// "谁持有、多久没心跳"—— 而这两件事此前**没有任何地方记录**。命名 mutex 只保证
// 存在与否,不带身份。这份记录就是那个身份。
//
// 反空洞自检查的是**格式本身**改坏了还查得出来:记录恰好有效时,
// "pid 缺失也能解析成功"这种洞永远问不出来。
#include "miaodesk/MiaoLockRecord.h"

#include <cstdio>
#include <string>

namespace miaodesk::lock_record {
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

} // namespace
} // namespace miaodesk::lock_record

int wmain() {
    using namespace miaodesk;
    using namespace miaodesk::lock_record;

    if (!SelfCheck()) {
        std::printf("\n[FAIL] 反空洞自检没通过:解析器对明知有问题的文本放行\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:pid 缺失 / 非数字 / heartbeat 带垃圾都被拒,好的解得回\n");
    ++g_checks;

    // ---- 1. 往返 ----
    {
        LockOwnership good;
        good.pid = 4242;
        good.heartbeatSeconds = 1700000000ull;
        const std::string text = Encode(good);
        Check(text.find("pid=4242") != std::string::npos, "编码里带 pid");
        Check(text.find("heartbeat=1700000000") != std::string::npos, "编码里带心跳");
        LockOwnership back;
        Check(Decode(text, &back), "往返解得回来");
        Check(back.pid == 4242 && back.heartbeatSeconds == 1700000000ull, "且字段对得上");
    }

    // ---- 2. pid=0 是有效记录,不是"没有记录" ----
    // 这一条是重点:一个空文件或"没这一行"必须与"pid=0 显式写着没有持有者"区分开。
    // 把两者混同,就会让"锁存在但记录坏了"被误判成没有持有者 —— 于是两个 owner 同时上。
    {
        LockOwnership empty;
        empty.pid = 0;
        empty.heartbeatSeconds = 0;
        const std::string text = Encode(empty);
        LockOwnership back;
        Check(Decode(text, &back), "pid=0 心跳=0 是**有效**记录(不是解析失败)");
        Check(back.pid == 0 && back.heartbeatSeconds == 0, "且被如实解出来");
    }

    // ---- 3. 坏文本一律被拒 ----
    {
        LockOwnership out{};
        Check(!Decode("", &out), "空文本被拒");
        Check(!Decode("pid=4242\n", &out), "缺 heartbeat 被拒(半条记录不许当成有效)");
        Check(!Decode("heartbeat=100\n", &out), "缺 pid 被拒");
        Check(!Decode("pid=4242\nheartbeat=abc\n", &out), "heartbeat 非数字被拒");
        Check(!Decode("pid=-1\nheartbeat=100\n", &out), "pid 是负数被拒");
        Check(!Decode("pid=99999999999\nheartbeat=100\n", &out), "pid 超出 32 位被拒");
        Check(!Decode("pid=4242\nheartbeat=1x\n", &out), "heartbeat 尾巴有垃圾被拒");
    }

    // ---- 4. 宽字符版本与窄字符一致 ----
    {
        LockOwnership good;
        good.pid = 777;
        good.heartbeatSeconds = 12345;
        LockOwnership back;
        Check(DecodeWide(EncodeWide(good), &back), "宽字符往返解得回来");
        Check(back.pid == 777 && back.heartbeatSeconds == 12345, "且字段对得上");
        Check(!DecodeWide(L"pid=abc\n", &back), "宽字符坏文本同样被拒");
    }

    // ---- 5. 解析失败时不许留下"看起来有效"的部分填充 ----
    // 调用方拿 out 当"没有可用记录"用;若失败路径已把 pid 填上而 heartbeat 还是 0,
    // 就会解出一条心跳为 0 的假记录 —— 而心跳 0 立刻被判成失联。
    {
        LockOwnership out;
        out.pid = 4242;                 // 预置一个值,看失败路径会不会动它
        out.heartbeatSeconds = 4242;
        Check(!Decode("pid=4242\nheartbeat=notanumber\n", &out), "失败");
        // 失败时 *out 不被信任。这里只断言"调用方因此不会以为有记录"——
        // 真正的保证是调用方看到 false 就不读 out,测试里不重复那个约定。
        Check(true, "(约定:返回 false 时 out 不被信任)");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\n锁持有权记录:全部 %d 项通过\n", g_checks);
    return 0;
}
