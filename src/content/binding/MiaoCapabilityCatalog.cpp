#include "miaodesk/MiaoCapabilityCatalog.h"

#include "miaodesk/MiaoInputBus.h"
#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"

#include <array>
#include <cstdio>

namespace miaodesk::content {
namespace {

using ContentBackend::D2D;
using ContentBackend::D3D11;
using ContentBackend::Host;
using ContentBackend::None;

// 两个后端都画
constexpr std::uint32_t kBoth = BackendBit(D2D) | BackendBit(D3D11);

// 下面每一条都对应一处真实代码。写这张表时按三类分:
//   * 会执行的 —— 注明后端;
//   * 只声明的 —— 注明"为什么它不执行",以及将来靠哪个任务补齐;
//   * 已经不做本轮范围的 —— 显式写成 DeclaredOnly,不悄悄留一个能通过校验的空洞。
//
// `deviceVerified` 一律为 false。目录只描述代码事实,真机签收走 PRO-05 的证据链,
// 不在这里预先勾上。
constexpr std::array<CapabilityEntry, 68> kCatalog{{
    // ---- 清单能力:manifest.json 的 capabilities[] ----
    {"clock.read", 1, CapabilityCarrier::ManifestCapability, BackendBit(D2D),
     {true, true, true, true, false}, "配 {{time.*}} 数据;时钟按分钟边界刷新",
     "", "由 capability broker 强制:声明了才读得到 time.*"},
    {"weather.read", 1, CapabilityCarrier::ManifestCapability, BackendBit(D2D),
     {true, true, true, true, false}, "读缓存快照;15 分钟刷新,失败 90 秒重试",
     "", "天气来自网络,失败时组件显式说明,不用假数据冒充在线"},
    {"tasks.read", 1, CapabilityCarrier::ManifestCapability, BackendBit(D2D),
     {true, true, true, true, false}, "最多 4 个槽位;32 条以内,标题 160 字",
     "", "读 TodayTaskStore;组件路径是真数据"},
    // 三个官方壁纸都在声明它,而加载器接受它。它不门禁任何东西 —— 这一点必须写出来,
    // 因为"声明了某个读权限"听起来像是在要求什么,而它什么也不要求。
    {"theme.wallpaper", 1, CapabilityCarrier::ManifestCapability, BackendBit(None),
     {true, false, true, true, false}, "无", "",
     "官方壁纸在声明的包级标识,当前没有任何执行点读它。保留是为了让已发行的三个"
     "包继续通过校验;新增内容不应依赖它做什么。"},

    // ---- 场景组件:scene.json 的 components[].kind ----
    {"scene.transform", 1, CapabilityCarrier::SceneComponent, kBoth,
     {true, true, true, true, false}, "position ±1e6,scale ±64,opacity [0,1]", "",
     "父链相乘;两个后端一致"},
    {"scene.spriteRenderer", 1, CapabilityCarrier::SceneComponent, kBoth,
     {true, true, true, true, false}, "25 MiB/图;cornerRadius 仅 D2D", "asset.image",
     "D3D11 只画第一个可见的 sprite(FindRenderable),多精灵会静默丢失"},
    {"scene.textRenderer", 1, CapabilityCarrier::SceneComponent, BackendBit(D2D),
     {true, true, true, true, false}, "fontSize 1..512;支持 {{data.path}} 模板", "",
     "只有 D2D 有文字路径;D3D11 上声明即通过但不画"},
    {"scene.videoRenderer", 1, CapabilityCarrier::SceneComponent, BackendBit(None),
     {true, false, false, true, false}, "无", "",
     "零像素:scene 里没有媒体基础,视频只有独立的壁纸路径。WALL-01 前必须给出"
     "『拒绝或实现』的明确结论,不留在能通过校验的空洞里"},
    {"scene.material", 1, CapabilityCarrier::SceneComponent, BackendBit(None),
     {true, false, false, false, false}, "无", "",
     "材质是顶层 resources,不在节点上;这个 kind 不携带数据,也没有行为"},
    {"scene.particleSystem", 1, CapabilityCarrier::SceneComponent, kBoth,
     {true, true, true, true, false}, "每 emitter 65536,每场景 131072;分析型 4096/16384",
     "asset.image", "这个 kind 本身不画粒子,只把包路由到 GPU 后端;粒子由顶层"
     "particleEmitters[] 描述"},
    {"scene.animator", 1, CapabilityCarrier::SceneComponent, BackendBit(None),
     {true, false, false, false, false}, "无", "",
     "动画数据在顶层 animations[];这个 kind 不携带数据,也没有行为"},
    {"scene.script", 1, CapabilityCarrier::SceneComponent, BackendBit(None),
     {true, false, false, false, false}, "无", "",
     "加载器与执行器都不存在。作者不应写它;写了的包应当被拒绝而不是被忽略"},
    {"scene.inputBinding", 1, CapabilityCarrier::SceneComponent, BackendBit(None),
     {true, false, false, false, false}, "无", "",
     "绑定是顶层 bindings[];这个 kind 不携带数据,也没有行为"},
    {"scene.custom", 1, CapabilityCarrier::SceneComponent, BackendBit(None),
     {true, false, false, false, false}, "无", "", "捕获性 kind,没有定义好的语义"},

    // ---- 资产类型:assets[].type ----
    {"asset.image", 1, CapabilityCarrier::AssetType, kBoth, {true, true, true, true, false},
     "25 MiB/图", "", "两个后端都能贴"},
    // 写这张表时它第一版被标成"D2D 可执行",那是错的:textRenderer 用的是**系统字体名**
    // (fontFamily 属性),没有任何渲染器读 asset:// 的 font 资源。标错的后果正是目录
    // 要防的那件事 —— 作者导入一个字体文件,以为文字会用上它。
    {"asset.font", 1, CapabilityCarrier::AssetType, BackendBit(None),
     {true, false, false, true, false}, "无", "",
     "解析与序列化支持,但没有任何渲染器读它:textRenderer 用 fontFamily 指定的系统字体名,"
     "不是字体资源。要支持导入字体是 WPRO-02 的独立工作"},
    {"asset.shader", 1, CapabilityCarrier::AssetType, BackendBit(D3D11),
     {true, true, true, true, false}, "vs_5_0 / ps_5_0;参数块 16 槽;不允许 compute",
     "material.programmable", "自定义 HLSL 目前对内容包开放,这与 ADV-04"
     "『先隔离后开放 AI Shader』是冲突项:authoring 的隔离尚未实现"},
    {"asset.video", 1, CapabilityCarrier::AssetType, BackendBit(None),
     {true, false, false, false, false}, "250 MiB/视频(独立壁纸路径)", "",
     "scene 内不作为贴图;校验器接受了它,而渲染器只吃图片"},
    {"asset.audio", 1, CapabilityCarrier::AssetType, BackendBit(None),
     {true, false, false, false, false}, "无", "", "scene 不加载本地音频资源"},
    {"asset.script", 1, CapabilityCarrier::AssetType, BackendBit(None),
     {true, false, false, false, false}, "无", "", "声明式内容不执行代码"},
    {"asset.mesh", 1, CapabilityCarrier::AssetType, BackendBit(None),
     {true, false, false, false, false}, "无", "", "3D 网格属 S6;渲染器当前明确拒绝 3D"},
    {"asset.binary", 1, CapabilityCarrier::AssetType, BackendBit(None),
     {true, false, false, false, false}, "无", "", "没有消费方"},

    // ---- 输入通道:scene.json 的 inputs[].id ----
    {"input://frame/time", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01;[0,1]", "", "帧调度器写;两个后端都读"},
    {"input://audio/level", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01", "", "WASAPI loopback → FFT → 平滑"},
    {"input://audio/bass", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01", "", ""},
    {"input://audio/lowmid", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01", "", ""},
    {"input://audio/mid", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01", "", ""},
    {"input://audio/highmid", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01", "", ""},
    {"input://audio/treble", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01", "", ""},
    {"input://audio/beat", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "BoolEdge;检测到 onset 的那一帧为真", "",
     "发布器会在 false 帧清除,所以下一次 onset 才是真边沿"},
    {"input://audio/spectrum", 1, CapabilityCarrier::InputChannel, BackendBit(None),
     {true, false, false, false, false}, "16 bin", "",
     "频谱算得出来但从未发布:发布器只写频段/电平/节拍。要么补发布,要么从契约里去掉"},
    {"input://pointer/x", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01;相对所属显示器", "",
     "位置通道不影响 click-through;D3D11 走私有直读,未经归一化与平滑"},
    {"input://pointer/y", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "Float01;相对所属显示器", "", "同上"},
    {"input://pointer/inside", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "BoolState", "", "不影响 click-through"},
    {"input://pointer/down", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "BoolState", "",
     "要求壁纸退出 click-through;只有声明了它才写入"},
    {"input://event/pointer/click", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "BoolEdge", "", "同上:此通道一出现就不再透传点击"},
    {"input://event/pointer/enter", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "BoolEdge", "", ""},
    {"input://event/pointer/leave", 1, CapabilityCarrier::InputChannel, kBoth,
     {true, true, true, true, false}, "BoolEdge", "", ""},
    {"input://event/pulse", 1, CapabilityCarrier::InputChannel, BackendBit(None),
     {true, false, false, false, false}, "BoolEdge", "",
     "契约里有它,但没有任何生产者 —— 自测之外没有人写它"},

    // ---- 材质 ----
    {"material.builtin.solidColor", 1, CapabilityCarrier::MaterialModel, kBoth,
     {true, true, true, true, false}, "color 属性", "",
     "唯一被两个后端都实现的 builtin model"},
    {"material.programmable", 1, CapabilityCarrier::MaterialModel, BackendBit(D3D11),
     {true, true, true, true, false}, "参数块 16 槽;compute 拒绝;入口点须为 C 标识符",
     "asset.shader", "D2D 明确拒绝这类包,不静默降级"},

    // ---- 后处理 ----
    {"postprocess.copy", 1, CapabilityCarrier::PostProcessEffect, BackendBit(D3D11),
     {true, true, true, true, false}, "amount", "", ""},
    {"postprocess.vignette", 1, CapabilityCarrier::PostProcessEffect, BackendBit(D3D11),
     {true, true, true, true, false}, "amount", "", ""},
    {"postprocess.noise", 1, CapabilityCarrier::PostProcessEffect, BackendBit(D3D11),
     {true, true, true, true, false}, "amount", "", ""},
    {"postprocess.colorMatrix", 1, CapabilityCarrier::PostProcessEffect, BackendBit(D3D11),
     {true, true, true, true, false}, "amount(饱和度插值)", "", ""},
    {"postprocess.blurHorizontal", 1, CapabilityCarrier::PostProcessEffect, BackendBit(D3D11),
     {true, true, true, true, false}, "amount, radius", "", "5 抽头可分离高斯的一半"},
    {"postprocess.blurVertical", 1, CapabilityCarrier::PostProcessEffect, BackendBit(D3D11),
     {true, true, true, true, false}, "amount, radius", "", "与上面一半合成一次模糊"},
    {"postprocess.bloomThreshold", 1, CapabilityCarrier::PostProcessEffect, BackendBit(D3D11),
     {true, true, true, true, false}, "amount, softness", "",
     "必须配 bloomCombine;孤立的阈值会被编译拒绝"},
    {"postprocess.bloomCombine", 1, CapabilityCarrier::PostProcessEffect, BackendBit(D3D11),
     {true, true, true, true, false}, "amount", "postprocess.bloomThreshold",
     "真分支:回读阈值那一遍的快照做 aux 输入"},

    // ---- 绑定响应曲线 ----
    {"binding.response.linear", 1, CapabilityCarrier::BindingResponse, kBoth,
     {true, true, true, true, false}, "* scale + offset", "", ""},
    {"binding.response.square", 1, CapabilityCarrier::BindingResponse, kBoth,
     {true, true, true, true, false}, "", "", ""},
    {"binding.response.cube", 1, CapabilityCarrier::BindingResponse, kBoth,
     {true, true, true, true, false}, "", "", ""},
    {"binding.response.sqrt", 1, CapabilityCarrier::BindingResponse, kBoth,
     {true, true, true, true, false}, "", "", "JSON 键名是 sqrt,不是 squareRoot"},
    {"binding.response.smoothstep", 1, CapabilityCarrier::BindingResponse, kBoth,
     {true, true, true, true, false}, "", "", ""},
    {"binding.response.elastic", 1, CapabilityCarrier::BindingResponse, kBoth,
     {true, true, true, true, false}, "峰值可超过 1,需要时调用方自己 clamp", "", ""},
    {"binding.response.threshold", 1, CapabilityCarrier::BindingResponse, kBoth,
     {true, true, true, true, false}, "中点硬切换", "", ""},
    {"binding.response.invert", 1, CapabilityCarrier::BindingResponse, kBoth,
     {true, true, true, true, false}, "", "", ""},

    // ---- 数据前缀:{{data.xxx}} ----
    {"data.time", 1, CapabilityCarrier::DataPathPrefix, BackendBit(D2D),
     {true, true, true, true, false}, "9 个 time.* 路径", "clock.read",
     "只经 TextRenderer 的模板;没有数值绑定路径"},
    {"data.weather", 1, CapabilityCarrier::DataPathPrefix, BackendBit(D2D),
     {true, true, true, true, false}, "读缓存快照", "weather.read", "同上"},
    {"data.tasks", 1, CapabilityCarrier::DataPathPrefix, BackendBit(D2D),
     {true, true, true, true, false}, "4 个槽位", "tasks.read", "同上"},

    // ---- 创作者工具 ----
    {"creator.tool.capabilities_get", 1, CapabilityCarrier::CreatorTool, BackendBit(Host),
     {true, true, true, true, false}, "只读", "", "自述工具清单与当前阶段"},
    {"creator.tool.skill_get", 1, CapabilityCarrier::CreatorTool, BackendBit(Host),
     {true, true, true, true, false}, "每份 ≤64 KiB;闭集 allowlist", "", ""},
    {"creator.tool.package_read", 1, CapabilityCarrier::CreatorTool, BackendBit(Host),
     {true, true, true, true, false}, "需 relativePath;过工作区策略", "", ""},
    {"creator.tool.package_update", 1, CapabilityCarrier::CreatorTool, BackendBit(Host),
     {true, true, true, true, false}, "需 content;事务写入;expectedDigest 冲突显式失败",
     "", ""},
    {"creator.tool.asset_import", 1, CapabilityCarrier::CreatorTool, BackendBit(Host),
     {true, true, true, true, false}, "只接受宿主托管来源", "", ""},
    {"creator.tool.candidate_submit", 1, CapabilityCarrier::CreatorTool, BackendBit(Host),
     {true, true, true, true, false}, "需宿主计算的 digest,不接受模型提供的字符串", "", ""},
    {"creator.tool.preview_evidence", 1, CapabilityCarrier::CreatorTool, BackendBit(Host),
     {true, true, true, true, false}, "离屏 D2D;最多 6 帧;单帧 ≤1280 边;整轮 ≤8 MiB",
     "", "宿主侧曾把摘要绑错(D-1),现已由 MakeRenderedEvidenceSample 保证"},
    {"creator.tool.image_generate", 1, CapabilityCarrier::CreatorTool, BackendBit(None),
     {true, false, false, false, false}, "无", "",
     "没有图片 Provider,所以它**不可用**,而这个状态由宿主显式说出来,"
     "不是落进默认分支让失败长得像一次拒绝"},
}};

// 新增一条能力要同时改这个数:它是刻意的摩擦,防止有人只往表里加东西、不改文档导出。
static_assert(kCatalog.size() == 68, "新增能力必须同步更新文档导出与 Skill,并递增这一处计数");

} // namespace

std::string_view ToString(CapabilityCarrier carrier) noexcept {
    switch (carrier) {
    case CapabilityCarrier::ManifestCapability: return "manifest.capability";
    case CapabilityCarrier::SceneComponent: return "scene.component";
    case CapabilityCarrier::AssetType: return "scene.asset";
    case CapabilityCarrier::InputChannel: return "scene.input";
    case CapabilityCarrier::MaterialModel: return "scene.material";
    case CapabilityCarrier::PostProcessEffect: return "scene.postProcess";
    case CapabilityCarrier::BindingResponse: return "scene.binding.response";
    case CapabilityCarrier::DataPathPrefix: return "data.prefix";
    case CapabilityCarrier::CreatorTool: return "creator.tool";
    case CapabilityCarrier::ParameterType: return "parameter.type";
    }
    return "unknown";
}

std::string_view ToString(CapabilityQueryClass klass) noexcept {
    switch (klass) {
    case CapabilityQueryClass::Real: return "Real";
    case CapabilityQueryClass::DeclaredOnly: return "DeclaredOnly";
    case CapabilityQueryClass::BackendUnsupported: return "BackendUnsupported";
    case CapabilityQueryClass::Unknown: return "Unknown";
    }
    return "unknown";
}

const std::vector<CapabilityEntry>& CapabilityCatalog() noexcept {
    static const std::vector<CapabilityEntry> catalog(kCatalog.begin(), kCatalog.end());
    return catalog;
}

const CapabilityEntry* FindCapability(std::string_view id) noexcept {
    for (const auto& entry : CapabilityCatalog()) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

CapabilityQueryResult QueryCapability(std::string_view id, std::uint32_t backendMask) {
    CapabilityQueryResult result;
    const auto* entry = FindCapability(id);
    if (!entry) {
        result.klass = CapabilityQueryClass::Unknown;
        result.reason = "能力目录里没有这一条:" + std::string(id);
        return result;
    }
    result.entry = entry;
    if (entry->backends == BackendBit(ContentBackend::None)) {
        result.klass = CapabilityQueryClass::DeclaredOnly;
        result.reason = "这一条写得出来,但没有任何后端会执行它。" + std::string(entry->note);
        return result;
    }
    if (backendMask != 0 && (entry->backends & backendMask) == 0) {
        result.klass = CapabilityQueryClass::BackendUnsupported;
        result.reason = "这一条会执行,但不在被问的这个后端上。";
        return result;
    }
    result.klass = CapabilityQueryClass::Real;
    result.reason = "这一条会真实执行。";
    return result;
}

bool IsCatalogCapability(std::string_view id) noexcept {
    return FindCapability(id) != nullptr;
}

// 这些 id 全是 ASCII,所以宽窄转换是逐字符的平凡事,不需要引入编解码器。
std::string NarrowInputId(std::wstring_view wide) {
    std::string narrow;
    narrow.reserve(wide.size());
    for (const wchar_t ch : wide) {
        if (ch < 0x80) narrow.push_back(static_cast<char>(ch));
        else narrow.push_back('?');  // 通道 id 不是 ASCII 就说明它不在契约里
    }
    return narrow;
}

CapabilityQueryResult QueryChannelCapability(std::wstring_view channel) {
    return QueryCapability(NarrowInputId(channel));
}

const CapabilityEntry* FindDataPrefixCapability(std::wstring_view dataPath) noexcept {
    // 前缀只有三个,而且必须和 broker 说同一组名字。这里不做模糊匹配:
    // "time" 不带点不算前缀,"times." 也不算 —— 否则一个拼错的路径会被当成支持。
    for (const auto* prefix : {L"time.", L"weather.", L"tasks."}) {
        const std::size_t length = std::wcslen(prefix);
        if (dataPath.size() > length && dataPath.compare(0, length, prefix) == 0) {
            // 目录里的 id 带 data. 前缀,而 broker 一侧是裸前缀;两边说的是同一条。
            std::string id = "data.";
            id += NarrowInputId(std::wstring_view(prefix, length - 1));
            return FindCapability(id);
        }
    }
    return nullptr;
}

std::string CapabilityCatalogDocument() {
    std::string out = "# MiaoDesk 内容能力目录(自动导出)\n\n";
    out += "目录版本 " + std::to_string(CapabilityCatalogVersion()) + "；共 " +
           std::to_string(CapabilityCatalog().size()) + " 条,其中会执行 " +
           std::to_string(ExecutableCapabilityCount()) + " 条。\n\n";
    out += "这份文档由 `MiaoCapabilityCatalog.cpp` 导出,不是手抄表。改能力必须改那一处,"
           "然后重新导出；Skill、校验器与创作者工具都读同一份目录。\n\n";
    for (const auto& entry : CapabilityCatalog()) {
        out += "## " + std::string(entry.id) + "\n\n";
        out += "- 载体：" + std::string(ToString(entry.carrier)) + "\n";
        out += "- 版本：" + std::to_string(entry.version) + "\n";
        std::string backends;
        if (entry.backends & BackendBit(D2D)) backends += "D2D ";
        if (entry.backends & BackendBit(D3D11)) backends += "D3D11 ";
        if (entry.backends & BackendBit(Host)) backends += "Host ";
        if (backends.empty()) backends = "(无)";
        out += "- 后端：" + backends + "\n";
        out += "- 状态：可声明=" + std::string(entry.support.declarable ? "是" : "否") +
               " 可执行=" + std::string(entry.support.executable ? "是" : "否") +
               " 可预览=" + std::string(entry.support.previewable ? "是" : "否") +
               " AI可创作=" + std::string(entry.support.aiAuthorable ? "是" : "否") +
               " 真机已验=" + std::string(entry.support.deviceVerified ? "是" : "否") + "\n";
        if (!entry.limits.empty()) out += "- 限制：" + std::string(entry.limits) + "\n";
        if (!entry.dependsOn.empty()) out += "- 依赖：" + std::string(entry.dependsOn) + "\n";
        if (!entry.note.empty()) out += "- 说明：" + std::string(entry.note) + "\n";
        out += "\n";
    }
    return out;
}

std::size_t ExecutableCapabilityCount() noexcept {
    std::size_t count = 0;
    for (const auto& entry : CapabilityCatalog()) {
        if (entry.backends != BackendBit(ContentBackend::None)) ++count;
    }
    return count;
}

} // namespace miaodesk::content
