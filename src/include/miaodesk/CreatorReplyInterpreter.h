#pragma once

// CCA-05:从工具结果里认出"这一轮做出了什么",并说清凭据是什么。
//
// `ContentCreatorDialog` 此前从**模型的回复正文**里正则扫一个 `.mdwall` / `.mdwidget`
// 路径,再扫一个"看起来像内容包目录"的候选。猜中的代价不是难看,是**不可判定** ——
// 用户在正文里提到任何一个路径都会被当成这次生成的产物,于是"它到底做出来了没有"
// 取决于模型怎么说话。
//
// 这个模块把判据分成两种凭据,并要求宿主说清它用的是哪一种:
//
//   Receipt  —— 一段结构化回执行,且通过了宿主台账的核验。这是唯一能让宿主
//               **不猜**的凭据:回执来自工具调用,账目来自宿主自己,两边的摘要、
//               revision、会话、epoch 都对上,才认。
//   ProseScan —— 退而求其次:正文里一个路径,由宿主另外验证它(存在、在工作区内、
//               有 manifest、扩展名对得上 kind)。它仍然能用,但**不能单独**
//               驱动"可以应用"这个结论。
//
// 分成两种不是洁癖:把两者混成"找到了",用户就无法知道现在看到的东西是工具交回来的,
// 还是模型一句话里提到的 —— 而这两者的可信度差得很远。
//
// 它不 import Windows 头,于是"散文伪装成回执"这一类在本机就能验。
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "miaodesk/ContentCandidateLedger.h"

namespace miaodesk::creator {

enum class CreatorReplySource {
    None,
    Receipt,      // 宿主核验过的结构化回执
    ProseScan,    // 正文里扫到的路径,还需要宿主另外验证
};

const char* ToString(CreatorReplySource source) noexcept;

struct CreatorReplyReading {
    CreatorReplySource source{CreatorReplySource::None};
    std::string detail;                 // 给人看的一句:凭据是什么、为什么没采信
    // Receipt 时是宿主持账的那一份;ProseScan 时是正文里扫到的那个路径(未验证)。
    std::string candidateId;
    std::uint32_t revision{};
    std::string digest;
    std::string snapshotPath;
    std::string prosePath;
    // ProseScan 专属:这个凭据**不能**驱动"可以应用"。宿主必须另外验证路径,
    // 而即便验证通过,也没有 revision / digest 与它对账。
    bool trustworthyWithoutFurtherChecks{false};
};

// `miaodesk::creator` 里有两个同名枚举 `ContentCreatorKind`(`ContentCreatorBridge.h`
// 与 `CreationWorkflow.h`),而后者的头文件会拖进 Windows 类型。这个模块不 import
// Windows 头,所以这里**只收那个枚举的数值**(0=None / 1=Wallpaper / 2=Widget),
// 由调用方转换。这避开了一个既有的命名地雷,而不是把它再挖深一点。
enum : std::uint32_t {
    kCreatorKindNone = 0,
    kCreatorKindWallpaper = 1,
    kCreatorKindWidget = 2,
};

// 这种 kind 对应的包扩展名。
std::string_view CreatorPackageExtension(std::uint32_t kind) noexcept;

// 读一段工具结果。
//
// workspaceRoot 是宿主持有的当前工作区;kind 决定期望的包扩展名。
// ledger / sessionId / epoch 是宿主持账,用来核验回执。
CreatorReplyReading InterpretCreatorReply(std::string_view text, std::uint32_t kind,
                                          const std::string& workspaceRoot,
                                          const content::ContentCandidateLedger& ledger,
                                          std::string_view sessionId, std::uint64_t epoch);

// ProseScan 之后,宿主拿到的那个路径还需要过这几道。它们是纯判据:
// 真正去看盘是宿主的事,但它必须一个都少不了。
struct ProsePathFacts {
    bool exists{false};
    bool isDirectory{false};
    bool hasManifest{false};
    bool extensionMatchesKind{false};
    bool insideWorkspace{false};
};

bool ProsePathIsUsable(const ProsePathFacts& facts, std::string* reason);

} // namespace miaodesk::creator
