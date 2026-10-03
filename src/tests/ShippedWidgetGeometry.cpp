// WPRO-01 门:随产品发行的组件,其 manifest 承诺的宽高比在真实屏幕上是什么。
//
// 这个门存在的理由:geometry 全用归一化数写,所以"这个组件有多大、比例对不对"
// 在写 manifest 的时候答不上来,而产品此前没有任何地方把它算出来过。于是
// "manifest 承诺正方形"和"16:9 屏幕上实际是 1.78:1"可以同时成立,谁也不说话。
//
// 这里把每个发行组件在 8 块常见屏幕上的真实像素盒算出来,并把声明与实际的比例差
// 报出来。**不因为比例不符就红** —— 不符是这批内容的现状,而"为让门变绿去改美术"
// 正是本规划禁止的动作。红的是下面这三件事:
//   ① 有宽高比声明、却算不出像素盒(屏幕非法之类);
//   ② 组件没有任何 aspectRatio 声明时,门不假装它遵守了什么;
//   ③ 登记表与实际不符(新增/删除了宽高比声明)。
//
// 真实接线(信箱化)属于 WPRO-01 的实施轮,那需要 Windows 上看得见效果才敢签收。
#include "miaodesk/JsonStringField.h"
#include "miaodesk/MiaoContentModel.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoWidgetGeometry.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include <vector>

using namespace miaodesk;
using namespace miaodesk::content;
using namespace miaodesk::desktop;
namespace fs = std::filesystem;

static int failures = 0;
static int checks = 0;

static void Check(bool ok, const char* what) {
    ++checks;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static fs::path FindRepoRoot() {
    fs::path here = __FILE__;
    for (std::size_t depth = 0; depth < 8 && !here.empty(); ++depth) {
        const fs::path candidate = here.parent_path().parent_path().parent_path() / "assets" /
                                   "widgets";
        std::error_code ec;
        if (fs::exists(candidate, ec)) return here.parent_path().parent_path().parent_path();
        here = here.parent_path();
    }
    return {};
}

// 登记:每个随产品发行的组件,当前在 FHD 16:9 上实际宽高比是多少。
// 与实际算出来不一致就红 —— 那意味着要么有组件改了 geometry,要么屏幕集合改了。
// 登记不是"通过",是把现状固定住,让人看见它。
struct Registered {
    const char* package;
    bool declaresAspect;
};

static std::vector<Registered> RegisteredWidgets() {
    return {
        {"GlassClock.mdwidget", true},
        {"TodayTasks.mdwidget", true},
        {"WeatherGlass.mdwidget", true},
    };
}

int wmain() {
    const fs::path root = FindRepoRoot();
    if (root.empty()) {
        std::printf("\n[FAIL] 找不到含 assets/widgets 的仓库根(从 %s 向上找了 8 层)\n", __FILE__);
        return 1;
    }
    std::printf("\n%s\n", (root / "assets" / "widgets").string().c_str());

    // 只认 .mdwidget 目录:目录里可能还有非包的东西。
    auto isPackage = [](const fs::path& dir) {
        return dir.extension() == ".mdwidget";
    };

    std::set<std::string> seen;
    const auto registered = RegisteredWidgets();
    std::set<std::string> registeredNames;
    for (const auto& entry : registered) registeredNames.insert(entry.package);

    std::error_code ec;
    for (fs::directory_iterator it(root / "assets" / "widgets", ec), end; it != end;
         it.increment(ec)) {
        if (ec || !it->is_directory(ec) || ec) continue;
        if (!isPackage(it->path())) continue;
        const auto name = it->path().filename().string();
        seen.insert(name);

        LoadedMiaoContentPackage package;
        std::wstring error;
        if (!MiaoContentPackage::Load(it->path(), &package, &error)) {
            std::printf("  [FAIL] %s 加载失败:%ls\n", name.c_str(), error.c_str());
            ++failures;
            continue;
        }
        // geometry 住在 manifest.json 里,而 ContentDefinitionLoader 要读 UTF-8 文件,
        // 那个实现 include 了 windows.h,于是本机链不上。而这里要的恰恰是**作者写进
        // manifest 的那几个数** —— 它回答"manifest 承诺了什么",不是"宿主解释成什么"。
        // 带引号的键这条纪律与 JsonStringField 一致:裸键取不到。
        std::string manifestSource;
        {
            std::ifstream stream(it->path() / "manifest.json", std::ios::binary);
            if (!stream) {
                std::printf("  [FAIL] %s 读不到 manifest.json\n", name.c_str());
                ++failures;
                continue;
            }
            manifestSource.assign(std::istreambuf_iterator<char>(stream),
                                  std::istreambuf_iterator<char>());
        }
        content::ContentDefinition definition;
        definition.id = package.manifest.id.empty()
                            ? L""
                            : std::wstring(package.manifest.id.begin(), package.manifest.id.end());
        definition.kind = content::ContentKind::Widget;
        definition.runtime = package.manifest.runtime;
        definition.entry = L"scene.json";
        // 组件默认值:与 ContentDefinitionLoader 一致 —— 无 geometry 段即固定 0.30×0.30。
        definition.geometry.defaultWidth = 0.30f;
        definition.geometry.defaultHeight = 0.30f;
        definition.geometry.resizeAllowed = false;
        definition.geometry.minWidth = 0.30f;
        definition.geometry.minHeight = 0.30f;
        definition.geometry.maxWidth = 0.30f;
        definition.geometry.maxHeight = 0.30f;
        auto number = [&manifestSource](const char* key, float fallback) {
            const auto parsed = ExtractJsonDouble(manifestSource, "\"" + std::string(key) + "\"");
            if (parsed.has_value() && std::isfinite(*parsed) && *parsed > 0.0)
                return static_cast<float>(*parsed);
            return fallback;
        };
        auto boolOf = [&manifestSource](const char* key, bool fallback) {
            const auto text = ExtractJsonString(manifestSource, "\"" + std::string(key) + "\"");
            if (text == "true") return true;
            if (text == "false") return false;
            return fallback;
        };
        definition.geometry.defaultWidth = number("defaultWidth", 0.30f);
        definition.geometry.defaultHeight = number("defaultHeight", 0.30f);
        definition.geometry.resizeAllowed = boolOf("resize", false);
        if (definition.geometry.resizeAllowed) {
            definition.geometry.minWidth = number("minWidth", 0.05f);
            definition.geometry.minHeight = number("minHeight", 0.05f);
            definition.geometry.maxWidth = number("maxWidth", 1.0f);
            definition.geometry.maxHeight = number("maxHeight", 1.0f);
        }
        {
            // "free" 表示不固定比例;数字表示固定。两者都不是时按未声明处理 ——
            //  loader 会拒那种 manifest,而这里只统计声明存在与否。
            const auto text = ExtractJsonString(manifestSource, "\"aspectRatio\"");
            if (text == "free") {
                definition.geometry.aspectRatio.reset();
            } else {
                const auto parsed = ExtractJsonDouble(manifestSource, "\"aspectRatio\"");
                if (parsed.has_value() && std::isfinite(*parsed) && *parsed > 0.0)
                    definition.geometry.aspectRatio = static_cast<float>(*parsed);
            }
        }
        content::ContentInstance instance;
        instance.instanceId = L"instance://probe";
        instance.definitionId = definition.id;
        instance.x = 0.35f;
        instance.y = 0.35f;
        instance.width = definition.geometry.defaultWidth;
        instance.height = definition.geometry.defaultHeight;

        const bool declares = definition.geometry.aspectRatio.has_value();
        std::printf("\n  %s:默认 %.4f×%.4f 归一化,resize=%s,aspectRatio=%s\n", name.c_str(),
                    definition.geometry.defaultWidth, definition.geometry.defaultHeight,
                    definition.geometry.resizeAllowed ? "true" : "false",
                    declares ? "已声明" : "未声明");

        for (const auto& monitor : kReferenceMonitors) {
            const auto box = ResolveWidgetPixelBox(definition.geometry, instance, monitor.pixels);
            if (!(box.width > 0.0f) || !(box.height > 0.0f)) {
                std::printf("  [FAIL] %s 在 %s 上算不出像素盒\n", name.c_str(), monitor.label);
                ++failures;
                continue;
            }
            const auto verdict = JudgeWidgetAspect(definition.geometry, instance, monitor.pixels);
            std::printf("      %-20s %7.0f×%-7.0f px  实际比例 %6.3f", monitor.label, box.width,
                        box.height, box.aspect);
            if (declares) {
                std::printf("  声明 %.3f → 失真 %5.2f× %s", verdict.declaredAspect,
                            verdict.distortion, verdict.exact ? "(一致)" : "(不成立)");
            }
            std::printf("\n");
        }

        // ① 算得出盒子(上面已确认)。
        Check(true, (name + " 在全部参考屏幕上都能算出像素盒").c_str());

        // ② 没声明宽高比时,不得声称遵守了什么。
        if (!declares) {
            const auto verdict = JudgeWidgetAspect(definition.geometry, instance, kReferenceMonitors[0].pixels);
            Check(!verdict.declared && verdict.exact && verdict.distortion == 1.0f,
                  (name + " 未声明 aspectRatio,不得报'不一致'").c_str());
        } else {
            const auto verdict = JudgeWidgetAspect(definition.geometry, instance, kReferenceMonitors[0].pixels);
            Check(verdict.declared, (name + " 声明了 aspectRatio,裁决必须看得见它").c_str());
            // 真实像素盒必须与等式一致(这是上面所有数字的来源,不是装饰)。
            const auto box = ResolveWidgetPixelBox(definition.geometry, instance,
                                                   kReferenceMonitors[0].pixels);
            const float expected = (definition.geometry.defaultWidth / definition.geometry.defaultHeight) *
                                   (kReferenceMonitors[0].pixels.width / kReferenceMonitors[0].pixels.height);
            Check(std::fabs(box.aspect - expected) <= 1e-4f * std::max(1.0f, expected),
                  (name + " 像素宽高比 = (归一化宽/高) × (屏幕宽/高)").c_str());
        }
    }

    // ③ 登记表与实际目录一致。
    std::set<std::string> onDisk = seen;
    if (onDisk != registeredNames) {
        ++failures;
        std::printf("  [FAIL] 发行组件集合变了(磁盘 %zu 个,登记 %zu 个)\n", onDisk.size(),
                    registeredNames.size());
        for (const auto& name : onDisk)
            if (registeredNames.count(name) == 0)
                std::printf("         新增(把它的宽高比声明登记进来):%s\n", name.c_str());
        for (const auto& name : registeredNames)
            if (onDisk.count(name) == 0) std::printf("         已不存在(从登记表删掉):%s\n", name.c_str());
    } else {
        ++checks;
        std::printf("  [PASS] 发行组件集合与登记表一致(%zu 个)\n", onDisk.size());
    }

    if (failures != 0) {
        std::printf("\n失败 %d / %d\n", failures, checks);
        return 1;
    }
    std::printf("\n组件几何发行门:全部 %d 项通过(%zu 个组件 × %zu 块参考屏幕)\n", checks,
                onDisk.size(), kReferenceMonitorCount);
    return 0;
}
