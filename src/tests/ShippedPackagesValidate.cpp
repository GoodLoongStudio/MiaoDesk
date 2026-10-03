// CAP-03:随产品发行的包必须通过**包级**校验。
//
// 为什么这一份独立存在:BuiltinWallpaperPackages 走的是 load → deserialize →
// runtime validate → initialize(场景运行时那条链),它**不跑 ContentPackageValidator**。
// 而包级校验正是宣言层:manifest 字段、kind/runtime、entry 在不在包里、代码产物、
// capabilities[] 是否在能力目录里。这些规则改了,已发行的包会不会被误伤,此前没有任何
// 自动检查回答过。
//
// 一个真实例子:CAP-01 给加载器与 validator 加了"未知 capability 拒绝"。三个官方壁纸
// 声明的是 theme.wallpaper —— 它不在 capability broker 的三个数据能力里。如果它没被
// 登记进目录,这道门会立刻红,而不是让用户在升级后发现三个内置壁纸加载失败。
// 这正是"为了防止回归加的门造成了新回归"的标准形状,所以它值得一道门。
//
// 校验器不吃盘,只吃包内各部分,所以这里从磁盘读真实发行包并喂给它。
#include "miaodesk/ContentPackageValidator.h"
#include "miaodesk/ContentCandidateLedger.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace miaodesk::creator {
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

namespace fs = std::filesystem;

// 从本文件向上找到含 assets/ 与 examples/ 的仓库根。CMake 传绝对路径给编译器,所以
// __FILE__ 在 CI 上也是绝对的;向上走让答案与进程当前工作目录无关。
fs::path FindRepoRoot() {
    fs::path here = __FILE__;
    for (std::size_t depth = 0; depth < 8 && !here.empty(); ++depth) {
        if (fs::exists(here.parent_path().parent_path().parent_path() / "assets" /
                       "wallpapers")) {
            return here.parent_path().parent_path().parent_path();
        }
        here = here.parent_path();
    }
    return {};
}

// CandidatePart 的 relPath 约定是"包内相对、正斜杠、小写"。这里按同一个约定生成 ——
// 用绝对路径或原始大小写会让同一个候选每次看起来都是新的。
std::string NormalizeRelative(const fs::path& relative) {
    std::string text = relative.generic_string();
    for (auto& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

// 一个包的 CandidatePartRole 由路径形状决定,与封存逻辑同一套判据。
// 角色由路径形状决定,判据与封存/校验那一侧一致,并且**接受两种合法布局**:
// scene/scene.json(创作工作区)与 scene.json(随产品发行的包)。第一版只认前者,于是
// 摘要那一层报"快照里没有 scene 入口文件" —— 8 个发行包全部拿不到身份。
content::CandidatePartRole RoleFor(const fs::path& relative) {
    const std::string name = NormalizeRelative(relative);
    if (name == "manifest.json") return content::CandidatePartRole::Manifest;
    if (name == "parameters.json") return content::CandidatePartRole::Parameters;
    if (name.rfind("preview.", 0) == 0) return content::CandidatePartRole::Preview;
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0) {
        return content::CandidatePartRole::Scene;
    }
    return content::CandidatePartRole::Other;
}

bool ReadFile(const fs::path& path, std::string* out) {
    // ifstream 而不是平台专属的安全打开函数:这份测试要在 macOS 与 Windows CI 上都真跑,
    // 用 MSVC 专属接口等于把本机那一半关掉。
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    out->assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return !stream.bad();
}

// 把一个包目录读成候选各部分(校验器要的形态)。
bool CollectPackage(const fs::path& root, std::vector<content::CandidatePart>* parts) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) return false;
        if (!it->is_regular_file(ec) || ec) continue;
        const fs::path relative = fs::relative(it->path(), root, ec);
        if (ec || relative.empty()) continue;
        content::CandidatePart part;
        part.role = RoleFor(relative);
        part.relPath = NormalizeRelative(relative);
        if (!ReadFile(it->path(), &part.bytes)) return false;
        parts->push_back(std::move(part));
    }
    return true;
}

std::string Describe(const content::ContentValidationResult& result) {
    std::string out;
    for (const auto& issue : result.issues) {
        out += "\n      [" + std::string(ToString(issue.failure)) + "] " + issue.file + " " +
               issue.nodePath + ": " + issue.message;
    }
    return out;
}

void ValidateShipped(const fs::path& root, const std::string& label) {
    std::vector<content::CandidatePart> parts;
    if (!CollectPackage(root, &parts)) {
        Check(false, label + ": 读不到包内容");
        return;
    }
    if (parts.empty()) {
        Check(false, label + ": 包里没有任何文件");
        return;
    }
    const auto result = ValidateCandidatePackage(parts);
    Check(result.ok, label + ": 随产品发行的包必须通过包级校验" + Describe(result));
}

} // namespace
} // namespace miaodesk::creator

int wmain() {
    using namespace miaodesk::creator;

    const fs::path root = FindRepoRoot();
    if (root.empty()) {
        std::printf("\n[FAIL] 找不到含 assets/wallpapers 的仓库根(从 %s 向上找了 8 层)\n", __FILE__);
        return 1;
    }

    // 三个官方壁纸 + 三个官方组件 + 两个示例包。示例包也发行:它们是作者唯一的
    // 完整范例,一份新的校验规则把范例拒掉,比把用户包拒掉更糟。
    //
    // 只认 .mdwall / .mdwidget 目录:assets/wallpapers 下还有一个 assets/ 目录,
    // 它不是包。第一版就是那么写的,于是那一轮 9 个"包"里有一个是素材目录。
    auto isPackage = [](const fs::path& dir) {
        const std::string ext = dir.extension().string();
        return ext == ".mdwall" || ext == ".mdwidget";
    };
    struct Where {
        const char* dir;
        const char* label;
    };
    for (const auto where : {Where{"assets/wallpapers", "官方壁纸"},
                             Where{"assets/widgets", "官方组件"},
                             Where{"examples/content", "示例包"}}) {
        std::error_code ec;
        for (fs::directory_iterator it(root / where.dir, ec), end; it != end; it.increment(ec)) {
            if (ec || !it->is_directory(ec) || ec) continue;
            if (!isPackage(it->path())) continue;
            ValidateShipped(it->path(),
                            std::string(where.label) + " " + it->path().filename().string());
        }
    }

    if (g_failures != 0) {
        std::printf("\n失败 %d / %d\n", g_failures, g_checks);
        return 1;
    }
    std::printf("随产品发行的包:全部 %d 个通过包级校验\n", g_checks);
    return 0;
}
