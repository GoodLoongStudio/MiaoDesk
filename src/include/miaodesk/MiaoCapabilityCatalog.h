#pragma once

// CAP-01:运行时能力目录。
//
// 规划验收:真实、缺失、仅声明、后端不支持四类查询必须给出不同答案;未知能力被拒;
// Skill/工具/校验器与**同一份**目录一致。
//
// 为什么是"同一份":此前描述能力的地方有四份手抄表 —— 四份 Skill、包契约文档、
// CONTENT_CREATOR_AGENT_PLAN 的能力陈述,以及散在代码里的接受/拒绝分支。它们互相
// 之间没有绑定,于是出现过"宿主说这个工具可用,而它其实必然失败"这一类的事。目录
// 只有一份,其余全部从它导出或反向核对。
//
// 目录的态度是保守的:一条能力要么真的会执行,要么就明确标成"仅声明"。把声明当成
// 实现是最贵的错 —— 它让一个坏包看起来完全合法,而作者要等到桌面上才发现。
//
// 它是纯逻辑,不碰盘也不 import Windows 头,于是这些规则在本机就能真验。
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <cwchar>

namespace miaodesk::content {

// 内容能落到的执行后端。None 表示"没有后端会执行它"。
enum class ContentBackend : std::uint32_t {
    None = 0,
    D2D = 1u << 0,
    D3D11 = 1u << 1,
    // 宿主侧能力:不画像素,但由宿主真实执行(创作者工具、生命周期、注册表)。
    Host = 1u << 1u << 1u,
};

constexpr std::uint32_t BackendBit(ContentBackend backend) noexcept {
    return static_cast<std::uint32_t>(backend);
}

inline ContentBackend operator|(ContentBackend a, ContentBackend b) noexcept {
    return static_cast<ContentBackend>(BackendBit(a) | BackendBit(b));
}

// 载体:这条能力靠什么写出来。它也决定谁能查它 —— 场景能力在 scene.json 里查,
// 清单能力在 manifest.json 里查,两者同名不算同一条。
enum class CapabilityCarrier {
    ManifestCapability,  // manifest.json 的 capabilities[]
    SceneComponent,      // scene.json 的 components[].kind
    AssetType,           // assets[].type
    InputChannel,        // scene.json 的 inputs[].id
    MaterialModel,       // materials[].model
    PostProcessEffect,   // scene.json 的 postProcesses[]
    BindingResponse,     // bindings[].response
    DataPathPrefix,      // {{data.…}} 模板可读的数据前缀
    CreatorTool,         // 创作会话里的工具名
    ParameterType,       // parameters.json 的参数类型
};

std::string_view ToString(CapabilityCarrier carrier) noexcept;

// 五种支持状态。它们互相独立:能写出来不等于会执行,会执行不等于预览展示得出来,
// 而真机签收是另一件事(本轮目录里它一律为 false —— 没有真机证据就不写 true)。
struct CapabilitySupport {
    bool declarable{false};      // schema/校验器接受这个写法
    bool executable{false};      // 某个后端真的执行
    bool previewable{false};     // 预览与创作反馈真的展示它
    bool aiAuthorable{false};    // AI 可以经由当前工具创作它
    bool deviceVerified{false};  // 已在 Windows 实机签收
};

struct CapabilityEntry {
    std::string_view id;             // 稳定 id,不随版本重命名
    std::uint32_t version{1};        // 条目版本;语义变化要递增
    CapabilityCarrier carrier{};
    std::uint32_t backends{0};      // ContentBackend 位掩码:会执行它的后端
    CapabilitySupport support{};
    std::string_view limits;        // 范围与资源限制(可读文本,与内容评审同一组数)
    std::string_view dependsOn;     // 依赖的能力 id;空表示无依赖
    std::string_view note;          // 诊断说明:为什么它在这个状态
};

// 查询的四种答案。验收要求四者不可混淆,所以它们是枚举而不是布尔。
enum class CapabilityQueryClass {
    Real,                // 会执行:至少一个后端真的执行它
    DeclaredOnly,        // 写得出来,但没有任何后端执行
    BackendUnsupported,  // 会执行,但不在被问的这个后端上
    Unknown,             // 目录里没有这一条
};

std::string_view ToString(CapabilityQueryClass klass) noexcept;

struct CapabilityQueryResult {
    CapabilityQueryClass klass{CapabilityQueryClass::Unknown};
    const CapabilityEntry* entry{nullptr};
    std::string reason;
};

const std::vector<CapabilityEntry>& CapabilityCatalog() noexcept;

// 目录版本。任何条目语义变化都要递增,发布说明里引用它。
constexpr std::uint32_t CapabilityCatalogVersion() noexcept { return 1; }

const CapabilityEntry* FindCapability(std::string_view id) noexcept;

// 受控查询。backendMask 为 0 表示"不关心后端,只问它会不会执行"。
CapabilityQueryResult QueryCapability(std::string_view id, std::uint32_t backendMask = 0);

// 清单能力(manifest.json 的 capabilities[])必须是目录里的一条。这一条就是
// 关闭"虚构能力"的地方:在此之前 capability id 只做字符集校验,于是
// `audio.read` 这类名字能通过,包里静悄悄地什么都不做。
bool IsCatalogCapability(std::string_view id) noexcept;

// 宽串一侧的通道查询。InputBus 的常量是宽串,而目录用 UTF-8 窄串
// (JSON/schema 里的形态);这些 id 全是 ASCII,转换是平凡的。
CapabilityQueryResult QueryChannelCapability(std::wstring_view channel);

// 数据前缀查询:{{data.xxx}} 模板可读的前缀,以及它要求的清单能力。
// 返回 nullptr 表示这个前缀不受支持。
const CapabilityEntry* FindDataPrefixCapability(std::wstring_view dataPath) noexcept;

// 把目录导出成作者文档。与其维护第二份手抄表,这里就是文档的唯一来源。
std::string CapabilityCatalogDocument();

// 目录里"会执行"的能力数:创作者与校验器用它说得出自己覆盖了多少。
std::size_t ExecutableCapabilityCount() noexcept;

} // namespace miaodesk::content
