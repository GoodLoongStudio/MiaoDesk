#include "miaodesk/PiLaunchProfile.h"

namespace miaodesk {

// 普通聊天:今天的取值原样保留。任何改动都会让 tests/pi-launch-profile-isolation.mjs 变红。
const wchar_t kPiChatToolAllowlist[] =
    L"read,bash,edit,write,grep,find,ls,"
    L"settings_open,ppt_create,file_create,folder_list,file_open,image_generate,"
    L"wallpaper_validate_package,wallpaper_state_get,desktop_widget_list,"
    L"desktop_preview_wallpaper,desktop_preview_examples,content_skill_get";

// 专用创作(CCA-04 落地的目标形态,与 §5 的八个工具对应)。
// 注意它**不含** read/bash/edit/write/grep/find/ls:创作不继承通用 shell 与文件权限,
// 需要的包读写由受约束工具完成。这是"关闭通用工具后仍可完整制作"的前提,
// 两部分必须作为同一个可验证切换交付,所以现在先固定住 allowlist 的形状。
const wchar_t kPiCreatorToolAllowlist[] =
    L"content_skill_get,creator_capabilities_get,creator_package_read,creator_package_update,"
    L"creator_asset_import,creator_image_generate,creator_candidate_submit,creator_preview_evidence";

std::wstring PiLaunchProfile::Signature() const {
    std::wstring out = signatureSalt;
    out += L"|dir=";
    out += agentDir;
    out += L"|sess=";
    out += sessionDir;
    out += L"|cwd=";
    out += workingDirectory;
    out += L"|ext=";
    out += extensionPath;
    out += L"|tools=";
    out += toolAllowlist;
    return out;
}

std::wstring PiLaunchProfile::Describe() const {
    // 只描述形状,不吐凭据、不吐完整 URL、不吐 system prompt 全文。
    std::wstring out = mode == PiLaunchMode::Creator ? L"creator" : L"chat";
    out += L" tools=";
    out += std::to_wstring(toolAllowlist.size());
    out += L"chars agentDir=";
    out += agentDir;
    return out;
}

PiLaunchProfile MakeChatLaunchProfile(std::wstring agentDir) {
    PiLaunchProfile profile;
    profile.mode = PiLaunchMode::Chat;
    profile.agentDir = std::move(agentDir);
    profile.sessionDir = profile.agentDir;
    profile.workingDirectory.clear();   // 由 PiRuntime 填桌面目录,与今天一致
    profile.toolAllowlist = kPiChatToolAllowlist;
    profile.signatureSalt = L"chat-v1";
    return profile;
}

PiLaunchProfile MakeCreatorLaunchProfile(std::wstring agentDir, std::wstring workspaceRoot,
                                        std::wstring extensionPath) {
    PiLaunchProfile profile;
    profile.mode = PiLaunchMode::Creator;
    profile.agentDir = std::move(agentDir);
    // Pi 自己的 session 目录单独放。今天用 --no-session,所以这个目录起初不会被创建;
    // 一旦将来要保存会话,它已经和聊天的分开,不必再改启动路径。
    profile.sessionDir = profile.agentDir + L"\\sessions";
    // 工作目录就是该作品的工作区。Pi 的 cwd 参与路径解析,所以它本身就是隔离的一部分。
    profile.workingDirectory = std::move(workspaceRoot);
    profile.extensionPath = std::move(extensionPath);
    profile.toolAllowlist = kPiCreatorToolAllowlist;
    profile.signatureSalt = L"creator-v1";
    return profile;
}

} // namespace miaodesk
