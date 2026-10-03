// CAP-03 发行内容资产门:每一个声明的资产都有人用,同一份字节不打两遍。
//
// 规划的验收项里"资产不重复打包"这句话此前没有任何门。随产品发行的三个壁纸一共
// 带 2.7 MB 位图,而"其中有没有一份没人引用的""有没有两份内容完全相同"这两个问题
// 只能靠人眼翻 manifest —— 而人眼翻的结果不会进 CI。
//
// 走的是真实链路:Load → 反序列化 → Validate → MiaoAssetDatabase::Build。
// Build 为**每个声明过的**资产建一条记录(文件不在磁盘上就直接失败),而 dependents
// 由 shaders / materials / 组件属性反解出来,所以"没人引用"与"同一份字节打两遍"
// 都是现成的答案,不需要再猜一遍 JSON。
//
// 本轮实测:三壁纸 15 条资产记录、2.7 MB,used/dangling/duplicate 三项全为 0。
// 这道门把"当前是干净的"固定下来 —— 三个数字任何一个变了都会红并说出是哪一条。
#include "miaodesk/MiaoAssetDatabase.h"
#include "miaodesk/MiaoContentPackage.h"
#include "miaodesk/MiaoSceneRuntimeModel.h"
#include "miaodesk/MiaoSceneSerializer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
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

// 一个包的资产体检结论。
//
// 抽成函数而不是写在循环里,是为了下面那道反空洞自检能走**同一条**判定。
// 我第一版就是写在循环里的:把"无人引用"那一行删掉,整道门仍然全绿 ——
// 因为当前内容没有无人引用的资产,于是"检查还在不在"这件事根本问不出来。
// 一个永远不失败的门,与一道好门在通过时长得一模一样。
struct AssetAudit {
    std::size_t records{};
    std::uintmax_t bytes{};
    std::vector<std::wstring> dangling;
    std::vector<std::wstring> duplicated;
    bool clean() const { return dangling.empty() && duplicated.empty(); }
};

AssetAudit AuditPackageAssets(const SceneRuntimeDefinition& runtime, const MiaoAssetDatabase& assets) {
    AssetAudit audit;
    std::map<std::uint64_t, std::vector<std::wstring>> byHash;
    for (const auto& asset : runtime.scene.assets) {
        const auto* record = assets.Find(asset.id);
        if (!record) continue;
        ++audit.records;
        audit.bytes += record->size;
        if (record->dependents.empty()) audit.dangling.push_back(record->id);
        byHash[record->contentHash].push_back(record->id);
    }
    for (const auto& [hash, ids] : byHash) {
        if (ids.size() > 1) audit.duplicated.push_back(ids.front());
    }
    return audit;
}

static fs::path FindRepoRoot() {
    fs::path here = __FILE__;
    for (std::size_t depth = 0; depth < 8 && !here.empty(); ++depth) {
        const fs::path candidate = here.parent_path().parent_path().parent_path() / "assets" /
                                   "wallpapers";
        std::error_code ec;
        if (fs::exists(candidate, ec)) return here.parent_path().parent_path().parent_path();
        here = here.parent_path();
    }
    return {};
}

// 反空洞自检:喂一个**必定**该被抓到的坏包,确认判定还动得了。
//
// 两个合成包各坏一处:一个声明了没人引的资产,一个把同一份字节声明两遍。
// 少了这一段,上面那道门在当前内容干净时永远绿 —— 而"永远绿"与"真的在查"
// 在通过的那一刻长得一模一样。第一版就是那么写的,变异检测把"无人引用"那行
// 删掉之后门照样全绿,才发现这里有个洞。
static bool SelfCheck() {
    const fs::path tempRoot = fs::temp_directory_path() / L"MiaoDesk-ShippedPackageAssets-SelfCheck";
    std::error_code ec;
    fs::remove_all(tempRoot, ec);
    if (!fs::create_directories(tempRoot / "assets", ec)) return false;
    bool ok = true;
    auto write = [&tempRoot](const wchar_t* name, const std::string& bytes) {
        std::ofstream out(tempRoot / "assets" / name, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return out.good();
    };
    auto scene = [](std::vector<AssetDefinition> assets, std::vector<SceneComponentDefinition> comps) {
        SceneRuntimeDefinition runtime;
        runtime.scene.id = L"scene://self-check";
        runtime.scene.kind = ContentKind::Wallpaper;
        runtime.scene.rootNodeId = L"node://root";
        runtime.scene.assets = std::move(assets);
        SceneNodeDefinition root;
        root.id = L"node://root";
        root.components = std::move(comps);
        runtime.scene.nodes.push_back(std::move(root));
        runtime.profile = RuntimeProfile::Wallpaper;
        return runtime;
    };

    // 坏处一:声明了一份没有任何组件引用的资产。
    if (write(L"dangling.png", "dangling")) {
        auto runtime = scene({AssetDefinition{L"asset://self-check/dangling", AssetType::Image,
                                             L"assets/dangling.png"}},
                             {});
        MiaoAssetDatabase assets;
        std::wstring error;
        if (!assets.Build(tempRoot, runtime, &error)) {
            ok = false;
        } else {
            const auto audit = AuditPackageAssets(runtime, assets);
            if (audit.dangling.empty() || audit.clean()) ok = false;
        }
    } else {
        ok = false;
    }

    // 坏处二:同一份字节声明两遍,两个 id 都被引用。
    if (write(L"a.png", "same-bytes") && write(L"b.png", "same-bytes")) {
        auto runtime = scene({AssetDefinition{L"asset://self-check/a", AssetType::Image,
                                             L"assets/a.png"},
                              AssetDefinition{L"asset://self-check/b", AssetType::Image,
                                             L"assets/b.png"}},
                             {});
        runtime.scene.nodes.front().components.push_back(SceneComponentDefinition{
            L"component://self-check/sprite", ComponentKind::SpriteRenderer,
            {PropertyDefinition{L"a", PropertyType::AssetReference,
                                AssetReference{L"asset://self-check/a"}},
             PropertyDefinition{L"b", PropertyType::AssetReference,
                                AssetReference{L"asset://self-check/b"}}}});
        MiaoAssetDatabase assets;
        std::wstring error;
        if (!assets.Build(tempRoot, runtime, &error)) {
            ok = false;
        } else {
            const auto audit = AuditPackageAssets(runtime, assets);
            if (audit.duplicated.empty() || audit.clean()) ok = false;
        }
    } else {
        ok = false;
    }

    // 干净的那一半也得是真的干净:每份资产都被引用且内容互不相同。
    if (write(L"c.png", "c-bytes") && write(L"d.png", "d-bytes")) {
        auto runtime = scene({AssetDefinition{L"asset://self-check/c", AssetType::Image,
                                             L"assets/c.png"},
                              AssetDefinition{L"asset://self-check/d", AssetType::Image,
                                             L"assets/d.png"}},
                             {});
        runtime.scene.nodes.front().components.push_back(SceneComponentDefinition{
            L"component://self-check/sprite", ComponentKind::SpriteRenderer,
            {PropertyDefinition{L"c", PropertyType::AssetReference,
                                AssetReference{L"asset://self-check/c"}},
             PropertyDefinition{L"d", PropertyType::AssetReference,
                                AssetReference{L"asset://self-check/d"}}}});
        MiaoAssetDatabase assets;
        std::wstring error;
        if (assets.Build(tempRoot, runtime, &error) && !AuditPackageAssets(runtime, assets).clean())
            ok = false;
    } else {
        ok = false;
    }

    fs::remove_all(tempRoot, ec);
    return ok;
}

int wmain() {
    const fs::path root = FindRepoRoot();
    if (root.empty()) {
        std::printf("\n[FAIL] 找不到含 assets/wallpapers 的仓库根(从 %s 向上找了 8 层)\n", __FILE__);
        return 1;
    }

    auto isPackage = [](const fs::path& dir) {
        const std::string ext = dir.extension().string();
        return ext == ".mdwall" || ext == ".mdwidget";
    };

    if (!SelfCheck()) {
        std::printf("\n[FAIL] 反空洞自检没通过:判定函数抓不住明知有问题的包\n");
        return 1;
    }
    std::printf("  [PASS] 反空洞自检:判定抓得住'无人引用'与'同一份字节打两遍'\n");
    ++checks;

    std::size_t totalRecords = 0;
    std::uintmax_t totalBytes = 0;
    std::size_t totalDangling = 0;
    std::size_t totalDuplicates = 0;

    struct Where {
        const char* dir;
        const char* label;
    };
    for (const auto where : {Where{"assets/wallpapers", "官方壁纸"},
                             Where{"assets/widgets", "官方组件"},
                             Where{"examples/content", "示例包"}}) {
        std::error_code ec;
        const fs::path base = root / where.dir;
        if (!fs::exists(base, ec)) continue;
        for (fs::directory_iterator it(base, ec), end; it != end; it.increment(ec)) {
            if (ec || !it->is_directory(ec) || ec) continue;
            if (!isPackage(it->path())) continue;
            const auto label = std::string(where.label) + " " + it->path().filename().string();

            std::wstring error;
            LoadedMiaoContentPackage package;
            if (!MiaoContentPackage::Load(it->path(), &package, &error)) {
                std::printf("  [FAIL] %s 加载失败:%ls\n", label.c_str(), error.c_str());
                ++failures;
                continue;
            }
            SceneRuntimeDefinition runtime;
            if (!MiaoSceneSerializer::DeserializePackage(package, &runtime, &error)) {
                std::printf("  [FAIL] %s 反序列化失败:%ls\n", label.c_str(), error.c_str());
                ++failures;
                continue;
            }
            MiaoAssetDatabase assets;
            if (!assets.Build(it->path(), runtime, &error)) {
                std::printf("  [FAIL] %s 资产库构建失败:%ls\n", label.c_str(), error.c_str());
                ++failures;
                continue;
            }

            ++checks;
            std::printf("  [PASS] %s 走通 load→反序列化→资产库\n", label.c_str());

            if (assets.Size() == 0) {
                std::printf("         (不引用任何资产)\n");
                continue;
            }

            // 没人引用的资产:它在包里占体积,而任何一个用户都看不见它的作用。
            // 注意这与"文件在磁盘上但没声明"是两件事 —— 后者 Build 根本不看。
            const auto audit = AuditPackageAssets(runtime, assets);
            totalBytes += audit.bytes;
            totalRecords += audit.records;
            totalDangling += audit.dangling.size();
            totalDuplicates += audit.duplicated.size();

            std::printf("         %zu 条资产、%.0f KB、无人引用 %zu 条、内容重复 %zu 组\n",
                        audit.records, static_cast<double>(audit.bytes) / 1024.0, audit.dangling.size(),
                        audit.duplicated.size());
            for (const auto& id : audit.dangling)
                std::printf("         [FAIL] 没有任何东西引用它:%ls\n", id.c_str());
            for (const auto& id : audit.duplicated)
                std::printf("         [FAIL] 同一份字节被打包了不止一次:%ls\n", id.c_str());

            Check(audit.clean(), (label + ": 每个声明的资产都有人引用,且没有重复内容").c_str());
        }
    }

    std::printf("\n合计:%zu 条资产记录、%.0f KB、无人引用 %zu 条、内容重复 %zu 处\n", totalRecords,
                static_cast<double>(totalBytes) / 1024.0, totalDangling, totalDuplicates);
    Check(totalRecords > 0, "门不是空的:真的审查到了资产(否则上面的循环一条都没跑)");

    if (failures != 0) {
        std::printf("\n失败 %d / %d\n", failures, checks);
        return 1;
    }
    std::printf("\n发行内容资产门:全部 %d 项通过\n", checks);
    return 0;
}
