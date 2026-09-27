#pragma once

#include <string>

// One display-name map for every Pi tool name, shared by the two surfaces that need it.
//
// It used to live inside ConversationPanelImpl.inc, where it was unreachable from any
// other translation unit. That was invisible until the AI creator needed the same
// strings: the creator receives the very same PiActivityEvent stream, and without this
// map it would either show a raw tool id (`content_skill_get`) or grow its own private
// copy of the table -- two tables that drift the moment a tool is added.
//
// Both surfaces' copy now lives here; there is one list to update.

namespace miaodesk::ai {

inline std::wstring FriendlyToolName(const std::wstring& raw) {
    if (raw == L"ppt_create") return L"制作 PowerPoint";
    if (raw == L"file_create") return L"创建文件";
    if (raw == L"folder_list") return L"查看文件夹";
    if (raw == L"file_open") return L"打开文件";
    if (raw == L"image_generate") return L"生成图片";
    if (raw == L"settings_open") return L"打开妙喵设置";
    if (raw == L"wallpaper_validate_package") return L"检查壁纸包";
    if (raw == L"desktop_preview_wallpaper") return L"准备壁纸预览";
    if (raw == L"desktop_preview_examples") return L"查看内置壁纸示例";
    if (raw == L"content_skill_get") return L"查阅内容创作规范";
    if (raw == L"wallpaper_state_get") return L"读取桌面状态";
    if (raw == L"desktop_widget_list") return L"查看桌面组件";
    if (raw == L"read") return L"读取文件";
    if (raw == L"write" || raw == L"edit") return L"编辑文件";
    if (raw == L"find" || raw == L"grep" || raw == L"ls") return L"查找内容";
    if (raw == L"bash") return L"执行系统操作";
    return raw.empty() ? L"执行操作" : raw;
}

} // namespace miaodesk::ai
