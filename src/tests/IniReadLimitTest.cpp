// P0-09「旧配置升级不丢 API profile」的读取边界回归。
//
// 逮到的不是"它会算错",而是**它在本机一行都验不到**:
// `ApiRuntimeProfile.h` 的 `ReadIni` 与 `ProfileSections` 都把 Win32 的返回值丢掉了。
// 而 GetPrivateProfileStringW / GetPrivateProfileSectionNamesW 在缓冲区放不下时
// **不报错** —— 它们在末尾写一个截断的字符串然后返回 nSize-2。上层于是拿到一段
// 看起来完全正常的文本。
//
// 三种用户可见后果:baseUrl 截断 → 请求打到另一台主机而 Key 也跟着去;
// 段名清单截断 → 整个 profile 从下拉里消失;model 截断 → 请求被服务端拒。
#include "miaodesk/MiaoIniReadLimit.h"

#include <cstdio>
#include <string>

namespace miaodesk {
namespace ini_read {
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
} // namespace ini_read
} // namespace miaodesk

int wmain() {
    using namespace miaodesk::ini_read;

    // ---- 1. 边界:MSDN 的 nSize-2 ----
    // 缓冲区 4096 字符(ReadIni 用的就是这个)。放得下的最长值是 4095。
    {
        Check(!ReadTruncated(4094, 4096), "4096 缓冲读到 4094 → 没截断(留得下结尾 null)");
        Check(ReadTruncated(4095, 4096), "4096 缓冲读到 4095 → 算可疑(顶到天了:正好写满与还有更多分不清)");
        Check(ReadTruncated(4096, 4096), "4096 缓冲读到 4096 → 截断(超出可容字符数)");
        Check(ReadTruncated(8000, 4096), "远超缓冲区 → 截断");
        Check(!ReadTruncated(0, 4096), "什么都没读到 → 不是截断(这个键可能本来就不存在)");
        Check(ReadTruncated(1, 2), "两字符缓冲读到 1 → 截断(= nSize-2)");
        Check(!ReadTruncated(1, 3), "三字符缓冲读到 1 → 没截断(放得下)");
    }

    // ---- 2. 退化输入 ----
    // 0 长度缓冲区的正确结论是"截断"(什么都放不下),不是 false。
    {
        Check(ReadTruncated(0, 0), "0 长度缓冲区 → 截断");
        Check(ReadTruncated(5, 0), "0 长度缓冲区 + 声称读了 5 → 截断(本身就是不可能的输入)");
    }

    // ---- 3. 段名清单那一侧 ----
    // ProfileSections 用 32768。多 profile 时这里是最先撞上的一侧:
    // 一旦截断,后面的整个 profile 从列表里消失。
    {
        Check(!ReadTruncated(32766, 32768), "32768 段名缓冲读到 32766 → 没截断");
        Check(ReadTruncated(32767, 32768), "32768 段名缓冲读到 32767 → 截断(= nSize-2)");
        Check(ReadTruncated(32768, 32768), "32768 段名缓冲读到 32768 → 截断");
    }

    // ---- 4. 真实的长度不是问题,问题是"不知道" ----
    // 这一条钉的是设计:短值绝大多数时候没问题,所以这个判据**必须**在
    // "看起来完全正常"的那些情况下也保持一致 —— 否则它就是个时灵时不灵的门。
    {
        for (std::size_t copied = 0; copied <= 64; ++copied) {
            for (std::size_t buffer = 1; buffer <= 64; ++buffer) {
                const bool truncated = ReadTruncated(copied, buffer);
                // 不变量:截断当且仅当"缓冲区连 copied+1 个字符加结尾 null 都放不下"
                //         —— 即 copied + 2 > buffer。
                const bool expected = (buffer == 0) || (copied + 1 >= buffer);
                if (truncated != expected) {
                    Check(false, "判据与期望不一致(copied=" + std::to_string(copied) +
                                     ",buffer=" + std::to_string(buffer) + ")");
                }
            }
        }
        Check(true, "穷举 0..64 × 0..64 全部与保守判据一致(buffer==0 或 copied+1>=buffer)");
    }

    // ---- 5. 那句话必须存在、必须说"内容不完整" ----
    // 用户看得见的就是它。截断的 profile 在下拉里和正常 profile 长得一模一样,
    // 所以这句话是这个缺陷唯一会让用户察觉的地方。
    {
        const std::string text = ExplainTruncatedRead();
        Check(!text.empty(), "截断有要说的话");
        Check(text.find("截断") != std::string::npos, "说出发生了什么(截断)");
        Check(text.find("不完整") != std::string::npos, "说出后果(配置可能不完整)");
        Check(text.find("缓冲区") != std::string::npos, "说出原因(内容超出缓冲区)");
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("\nINI 读取边界:全部 %d 项通过\n", g_checks);
    return 0;
}
