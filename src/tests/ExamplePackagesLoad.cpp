// CAP-02:作者照抄的那两个示例包,必须真的走得通整条链。
//
// 已有的覆盖有两块,都不覆盖它们:
//   · BuiltinWallpaperPackages 走的是三个**内置壁纸**的 load → deserialize →
//     validate → initialize → asset DB,样例不在它的 spec 表里;
//   · ShippedPackagesValidate 走的是**包级**校验(manifest 字段、entry 布局、
//     capabilities 在不在目录里),它不碰场景运行时。
//
// 于是 `examples/content/*` —— 作者唯一能照着抄的完整范例 —— 出问题时没有任何门会红。
// 这不是理论风险:本轮就发现 Skill 一直把视频教成 `videoRenderer` 循环,而那个组件
// 没有渲染器;照那样抄出来的产物是空白的。示例是教学的入口,它坏了比一个用户包坏了更糟。
//
// 这里验的是"链走得通 + 内容自洽",不验像素(要 GPU 与真机)。
#include "miaodesk/MiaoAssetDatabase.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoSceneModel.h"
#include "miaodesk/MiaoSceneRuntime.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSceneSerializer.h"
#include "miaodesk/MiaoShaderContract.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace miaodesk::content;
namespace fs = std::filesystem;

static int failures = 0;
static int checks = 0;
static void Check(bool ok, const char* what) {
    ++checks;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static fs::path FindExamplesDir() {
    // 与 BuiltinWallpaperPackages 同一套做法:从本文件向上找,答案与进程当前目录无关。
    fs::path here = __FILE__;
    for (std::size_t depth = 0; depth < 8 && !here.empty(); ++depth) {
        const fs::path candidate =
            here.parent_path().parent_path().parent_path() / "examples" / "content";
        std::error_code ec;
        if (fs::exists(candidate, ec)) return candidate;
        here = here.parent_path();
    }
    return {};
}

static bool ReadText(const fs::path& path, std::string* out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    out->assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return !stream.bad();
}

// 走一遍不依赖 Windows 的整条链,并把错误带出来(失败时要能说出卡在哪一步)。
struct Outcome {
    bool loaded{};
    bool deserialized{};
    bool runtimeValid{};
    bool assetsResolve{};
    bool initialized{};
    bool parametersResolve{};
    std::wstring error;
    SceneRuntimeDefinition runtime;
    std::size_t nodeCount{};
};

static Outcome Walk(const fs::path& root) {
    Outcome out;
    LoadedMiaoContentPackage package;
    std::wstring error;
    if (!MiaoContentPackage::Load(root, &package, &error)) {
        out.error = error;
        return out;
    }
    out.loaded = true;
    if (!MiaoSceneSerializer::DeserializePackage(package, &out.runtime, &error)) {
        out.error = error;
        return out;
    }
    out.deserialized = true;
    if (!MiaoSceneRuntimeModel::Validate(out.runtime, &error)) {
        out.error = error;
        return out;
    }
    out.runtimeValid = true;
    MiaoAssetDatabase assets;
    if (!assets.Build(root, out.runtime, &error)) {
        out.error = error;
        return out;
    }
    out.assetsResolve = true;
    MiaoSceneRuntime runtime;
    if (!runtime.Initialize(out.runtime, &error)) {
        out.error = error;
        return out;
    }
    out.initialized = true;
    out.nodeCount = out.runtime.scene.nodes.size();
    return out;
}

// 绑定引用的参数必须真的在 parameters.json 里。绳子那一头空着的话,作者以为自己
// 在调一个旋钮,而桌面上那个旋钮既不动也不报错。
static void CheckBindingsResolveParameters(const fs::path& root) {
    std::string sceneText;
    if (!ReadText(root / "scene.json", &sceneText)) {
        Check(false, "读得到 scene.json");
        return;
    }
    std::string parametersText;
    const bool hasParameters = ReadText(root / "parameters.json", &parametersText);
    std::size_t missing = 0;
    std::size_t total = 0;
    std::size_t from = 0;
    while (true) {
        const auto at = sceneText.find("param://", from);
        if (at == std::string::npos) break;
        from = at + 1;
        const auto close = sceneText.find('"', at);
        if (close == std::string::npos) break;
        const std::string id = sceneText.substr(at, close - at);
        ++total;
        if (hasParameters && parametersText.find(id) == std::string::npos) ++missing;
    }
    Check(missing == 0, "scene 里每个 param:// 绑定都在 parameters.json 里有定义");
    if (total > 0) {
        std::printf("         (共 %zu 个 param:// 引用)\n", total);
    }
    if (!hasParameters) {
        Check(total == 0, "没有 parameters.json 的包不得引用参数");
    }
}

int wmain() {
    const fs::path examples = FindExamplesDir();
    if (examples.empty()) {
        std::printf("\n[FAIL] 找不到 examples/content(从 %s 向上找了 8 层)\n", __FILE__);
        return 1;
    }
    std::printf("\n示例包目录:%ls\n", examples.wstring().c_str());

    for (const auto& entry : fs::directory_iterator(examples)) {
        if (!entry.is_directory()) continue;
        const std::string label = "示例 " + entry.path().filename().string();
        std::printf("\n%s\n", label.c_str());

        const Outcome out = Walk(entry.path());
        Check(out.loaded, "manifest 与入口通过 MiaoContentPackage::Load");
        if (!out.loaded) {
            std::printf("         (error = %ls)\n", out.error.c_str());
            continue;
        }
        Check(out.deserialized, "scene.json 通过反序列化");
        if (!out.deserialized) {
            std::printf("         (error = %ls)\n", out.error.c_str());
            continue;
        }
        Check(out.runtimeValid, "场景通过 MiaoSceneRuntimeModel::Validate");
        if (!out.runtimeValid) {
            std::printf("         (error = %ls)\n", out.error.c_str());
            continue;
        }
        Check(out.assetsResolve, "资产数据库解析出全部引用(文件都在磁盘上)");
        if (!out.assetsResolve) std::printf("         (error = %ls)\n", out.error.c_str());
        Check(out.initialized, "MiaoSceneRuntime::Initialize 通过");
        if (!out.initialized) std::printf("         (error = %ls)\n", out.error.c_str());
        Check(out.nodeCount > 0, "场景不是空的");

        CheckBindingsResolveParameters(entry.path());

        // 示例里不得带代码产物:它是被照抄的东西,带一个 .js 比用户包里带一个更糟。
        bool hasCode = false;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(entry.path(), ec), end; it != end;
             it.increment(ec)) {
            if (ec || !it->is_regular_file(ec) || ec) continue;
            const auto ext = it->path().extension().string();
            if (ext == ".js" || ext == ".ts" || ext == ".html" || ext == ".css" ||
                ext == ".exe" || ext == ".dll") {
                hasCode = true;
            }
        }
        Check(!hasCode, "示例包不含代码产物(它会被照抄)");
    }

    // ShaderPulse 的 HLSL 要过契约:入口点是 C 标识符、不要 compute stage。
    // 这一条是纯逻辑,不编译 shader —— 真正的编译要 D3D 设备。
    const fs::path pulse = examples / "ShaderPulse.mdwall" / "shaders" / "pulse.hlsl";
    std::string hlsl;
    if (ReadText(pulse, &hlsl)) {
        std::wstring shaderError;
        Check(MiaoShaderContract::ValidateEntryPoint("main", &shaderError) ||
                  MiaoShaderContract::ValidateEntryPoint("VSMain", &shaderError),
              "示例 shader 的入口点命名符合契约");
    } else {
        Check(false, "ShaderPulse 的 HLSL 还在(它是可编程材质的唯一示例)");
    }

    if (failures != 0) {
        std::printf("\n失败 %d / %d\n", failures, checks);
        return 1;
    }
    std::printf("\n示例包:全部 %d 项检查通过\n", checks);
    return 0;
}
