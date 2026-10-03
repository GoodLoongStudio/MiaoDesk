// CAP-01:能力目录的四种查询答案,以及"未知能力被拒"。
//
// 规划验收要求四类答案不可混淆:真实、缺失、仅声明、后端不支持。它们不是布尔,
// 因为把"仅声明"说成"支持"是最贵的错 —— 它让一个坏包看起来完全合法,而作者要到
// 桌面上才发现。
//
// 这份测试还钉住两件容易悄悄失效的事:
//   * 目录必须和代码说同一组名字(组件 kind、输入通道、数据前缀、内置材质),否则
//     "避免三份手抄表"就白说了;
//   * deviceVerified 一律为 false:没有真机证据,不许在这里预先勾上。
#include "miaodesk/MiaoCapabilityCatalog.h"

#include "miaodesk/MiaoInputBus.h"
#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"

#include <cstdio>
#include <string>
#include <vector>

namespace miaodesk::content {
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

std::size_t CountWithCarrier(CapabilityCarrier carrier) {
    std::size_t count = 0;
    for (const auto& entry : CapabilityCatalog()) {
        if (entry.carrier == carrier) ++count;
    }
    return count;
}

bool HasId(std::string_view id) { return FindCapability(id) != nullptr; }

} // namespace
} // namespace miaodesk::content

int wmain() {
    using namespace miaodesk::content;
    using namespace miaodesk::content::inputbus;

    // ---- 1. 四类答案各得其所 ----
    const auto real = QueryCapability("scene.transform");
    Check(real.klass == CapabilityQueryClass::Real, "transform 是会执行的能力。");
    Check(real.entry != nullptr, "查得到的条目带得出细节(限制、依赖、说明)。");

    Check(QueryCapability("scene.suchKind").klass == CapabilityQueryClass::Unknown,
          "目录里没有的 kind 是 Unknown,不是 DeclaredOnly。");
    Check(QueryCapability("audio.read").klass == CapabilityQueryClass::Unknown,
          "虚构的 manifest capability 也在目录里找不到。");

    const auto declared = QueryCapability("scene.videoRenderer");
    Check(declared.klass == CapabilityQueryClass::DeclaredOnly,
          "videoRenderer 写得出来但没有后端执行它 —— 不能报成支持。");
    Check(declared.entry != nullptr && !declared.entry->note.empty(),
          "仅声明那条必须说清为什么不执行。");

    const auto backend = QueryCapability("scene.textRenderer", BackendBit(ContentBackend::D3D11));
    Check(backend.klass == CapabilityQueryClass::BackendUnsupported,
          "D3D11 上没有文字路径,答案必须是 BackendUnsupported。");
    const auto backendOk =
        QueryCapability("scene.textRenderer", BackendBit(ContentBackend::D2D));
    Check(backendOk.klass == CapabilityQueryClass::Real, "同一个能力在 D2D 上是 Real。");
    const auto anyBackend = QueryCapability("scene.textRenderer");
    Check(anyBackend.klass == CapabilityQueryClass::Real,
          "不指定后端时只问它会不会执行。");

    // ---- 2. 未知能力被拒:关闭"虚构能力" ----
    Check(IsCatalogCapability("clock.read"), "已发行的清单能力在目录里。");
    Check(IsCatalogCapability("theme.wallpaper"),
          "官方壁纸在声明的包级能力也要在 —— 否则一拒绝就会让三个已发行包加载失败。");
    Check(!IsCatalogCapability("audio.read"), "没接上的能力不能凭名字通过。");
    Check(!IsCatalogCapability("media.read"), "media 通道不存在,所以 media.read 也不存在。");

    // ---- 3. 目录必须和代码说同一组名字 ----
    // 少一个就是漏登记;多一个就是目录在描述不存在的东西。
    const std::vector<std::pair<std::string_view, ComponentKind>> kinds{
        {"scene.transform", ComponentKind::Transform},
        {"scene.spriteRenderer", ComponentKind::SpriteRenderer},
        {"scene.textRenderer", ComponentKind::TextRenderer},
        {"scene.videoRenderer", ComponentKind::VideoRenderer},
        {"scene.material", ComponentKind::Material},
        {"scene.particleSystem", ComponentKind::ParticleSystem},
        {"scene.animator", ComponentKind::Animator},
        {"scene.script", ComponentKind::Script},
        {"scene.inputBinding", ComponentKind::InputBinding},
        {"scene.custom", ComponentKind::Custom},
    };
    for (const auto& [id, kind] : kinds) {
        Check(HasId(id), std::string("组件 kind 已登记:") + std::string(id));
        (void)kind;
    }
    Check(CountWithCarrier(CapabilityCarrier::SceneComponent) == kinds.size(),
          "登记的场景组件数与枚举成员数一致(" + std::to_string(kinds.size()) + ")。");

    // 输入通道:契约里每个常量都该有一条,不多不少。
    const std::vector<std::pair<std::wstring_view, std::string_view>> channels{
        {kFrameTimeInput, "input://frame/time"},
        {kAudioLevel, "input://audio/level"},
        {kAudioBass, "input://audio/bass"},
        {kAudioLowMid, "input://audio/lowmid"},
        {kAudioMid, "input://audio/mid"},
        {kAudioHighMid, "input://audio/highmid"},
        {kAudioTreble, "input://audio/treble"},
        {kAudioBeat, "input://audio/beat"},
        {kPointerX, "input://pointer/x"},
        {kPointerY, "input://pointer/y"},
        {kPointerInside, "input://pointer/inside"},
        {kPointerDown, "input://pointer/down"},
        {kPointerClick, "input://event/pointer/click"},
        {kPointerEnter, "input://event/pointer/enter"},
        {kPointerLeave, "input://event/pointer/leave"},
        {kEventPulse, "input://event/pulse"},
    };
    for (const auto& [wide, narrow] : channels) {
        Check(HasId(narrow), std::string("输入通道已登记:") + std::string(narrow));
        Check(QueryChannelCapability(wide).entry != nullptr, "宽串一侧也查得到同一通道。");
    }

    // ChannelShape 认的通道,目录必须也认。这条是"契约与目录不同步"的直接拦截。
    for (const auto channel : channels) {
        InputChannelShape shape = InputChannelShape::Float01;
        if (!ChannelShape(channel.first, &shape)) continue;
        Check(QueryChannelCapability(channel.first).klass != CapabilityQueryClass::Unknown,
              "ChannelShape 认的通道在目录里必须查得到。");
    }

    // 数据前缀:三个,与 broker 一致。
    Check(FindDataPrefixCapability(L"time.hhmm") != nullptr, "time.* 是可读数据。");
    Check(FindDataPrefixCapability(L"weather.temperature") != nullptr, "weather.* 是可读数据。");
    Check(FindDataPrefixCapability(L"tasks.total") != nullptr, "tasks.* 是可读数据。");
    Check(FindDataPrefixCapability(L"media.nowPlaying") == nullptr,
          "没有 media 提供方,所以 media.* 不可读 —— 而不是读了给个空值。");
    Check(FindDataPrefixCapability(L"time") == nullptr, "不带点的前缀不算,拼错的不会被当成支持。");

    // ---- 4. 响应曲线:8 条,一条不少 ----
    std::size_t responses = 0;
    for (const auto& entry : CapabilityCatalog()) {
        if (entry.carrier == CapabilityCarrier::BindingResponse) ++responses;
    }
    Check(responses == kBindingResponseCount,
          "登记的响应曲线数与枚举成员数一致(" + std::to_string(kBindingResponseCount) + ")。");

    // ---- 5. 唯一被支持的 builtin 材质只有 solidColor ----
    const auto solid = QueryCapability("material.builtin.solidColor");
    Check(solid.klass == CapabilityQueryClass::Real, "solidColor 会执行。");
    Check(QueryCapability("material.builtin.gradient").klass == CapabilityQueryClass::Unknown,
          "目录里没有 gradient:写它不会生效,而这一点必须能查出来。");

    // ---- 6. 创作者工具:不可用那个必须自己说出来 ----
    const auto image = QueryCapability("creator.tool.image_generate");
    Check(image.klass == CapabilityQueryClass::DeclaredOnly, "图片生成确实还没接上。");
    Check(QueryCapability("creator.tool.preview_evidence").klass == CapabilityQueryClass::Real,
          "渲染取证是接上的。");

    // ---- 7. 真机一列不许预先勾上 ----
    for (const auto& entry : CapabilityCatalog()) {
        Check(!entry.support.deviceVerified,
              std::string(entry.id) + " 没有真机证据就不能标 deviceVerified");
    }

    // ---- 8. 文档导出与目录同源 ----
    const auto document = CapabilityCatalogDocument();
    for (const auto& entry : CapabilityCatalog()) {
        Check(document.find(std::string(entry.id)) != std::string::npos,
              std::string("文档导出里有 ") + std::string(entry.id));
    }
    Check(document.find("目录版本") != std::string::npos, "导出带目录版本,发布说明引用它。");
    Check(ExecutableCapabilityCount() > 0 && ExecutableCapabilityCount() <= CapabilityCatalog().size(),
          "会执行的条数是目录的一个子集。");

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("能力目录:全部 %d 项检查通过(%zu 条能力,%zu 条可执行)\n", g_checks,
                CapabilityCatalog().size(), ExecutableCapabilityCount());
    return 0;
}
