from pathlib import Path


def require(text: str, marker: str, label: str) -> None:
    if marker not in text:
        raise RuntimeError(f"missing {label}: {marker!r}")


def patch_goz() -> None:
    header = Path("src/native/include/turingdesk/GozSearch.h")
    text = header.read_text(encoding="utf-8")
    anchor = "    bool Query(HWND replyWindow, const std::wstring& query, DWORD maxResults = 12) const;\n"
    if "QuerySync(" not in text:
        require(text, anchor, "GozSearch header anchor")
        text = text.replace(
            anchor,
            anchor + "    std::vector<SearchResult> QuerySync(const std::wstring& query, DWORD maxResults = 12) const;\n",
            1,
        )
        header.write_text(text, encoding="utf-8", newline="\n")

    source = Path("src/native/src/GozSearch.cpp")
    text = source.read_text(encoding="utf-8")
    anchor = "bool GozSearch::HandleCopyData(const COPYDATASTRUCT* copyData, std::vector<SearchResult>& results) const {\n"
    if "GozSearch::QuerySync(" not in text:
        require(text, anchor, "GozSearch source anchor")
        implementation = r'''std::vector<SearchResult> GozSearch::QuerySync(const std::wstring& query, DWORD maxResults) const {
    std::vector<SearchResult> results;
    if (query.empty() || maxResults == 0) return results;
    const auto binary = FindClientBinary();
    if (binary.empty() || !PipeAvailable()) return results;

    std::vector<std::wstring> paths;
    if (!RunGozQuery(binary, query, maxResults, paths)) return results;
    results.reserve(paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const auto& fullPath = paths[i];
        if (fullPath.empty()) continue;
        fs::path path(fullPath);
        std::wstring title = path.filename().wstring();
        if (title.empty()) title = fullPath;
        const DWORD attributes = GetFileAttributesW(fullPath.c_str());
        const bool directory = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
        results.push_back({directory ? ResultKind::Folder : ResultKind::File,
                           std::move(title), fullPath, fullPath,
                           500.0 - static_cast<double>(i)});
    }
    return results;
}

'''
        text = text.replace(anchor, implementation + anchor, 1)
        source.write_text(text, encoding="utf-8", newline="\n")


def patch_agent() -> None:
    source = Path("src/native/src/L3Agent.cpp")
    text = source.read_text(encoding="utf-8")
    include_anchor = '#include "turingdesk/L3Agent.h"\n'
    if '#include "turingdesk/AppSearch.h"' not in text:
        require(text, include_anchor, "L3Agent include anchor")
        text = text.replace(
            include_anchor,
            include_anchor
            + '#include "turingdesk/AppSearch.h"\n'
            + '#include "turingdesk/GozSearch.h"\n'
            + '#include <shellapi.h>\n',
            1,
        )

    old_help = 'reply = L"L3 命令：/status、/time、/new。模型和 API Key 请使用右上角 AI 设置；Ctrl+Enter 强制进入 L3。";'
    new_help = 'reply = L"L3 命令：/status、/time、/apps <关键词>、/files <关键词>、/open <应用名>、/open-file <文件名>、/new。模型和 API Key 请使用右上角 AI 设置。";'
    if old_help in text:
        text = text.replace(old_help, new_help, 1)
    require(text, "/apps <关键词>", "updated L3 help")

    anchor = '    if (lower.starts_with(L"/key ")) {\n'
    if 'lower.starts_with(L"/apps ")' not in text:
        require(text, anchor, "L3 local tool insertion anchor")
        tools = r'''    if (lower.starts_with(L"/apps ")) {
        const auto query = Trim(input.substr(6));
        if (query.empty()) { reply = L"用法：/apps <关键词>"; return true; }
        AppSearch apps;
        apps.BuildIndex();
        const auto results = apps.Query(query, 8);
        if (results.empty()) { reply = L"没有找到匹配的应用。"; return true; }
        reply = L"应用结果：";
        for (const auto& item : results) reply += L"\r\n- " + item.title;
        return true;
    }
    if (lower.starts_with(L"/files ")) {
        const auto query = Trim(input.substr(7));
        if (query.empty()) { reply = L"用法：/files <关键词>"; return true; }
        GozSearch files;
        if (!files.Available()) { reply = L"文件索引当前未就绪；应用搜索和其他 L3 本地工具仍可使用。"; return true; }
        const auto results = files.QuerySync(query, 8);
        if (results.empty()) { reply = L"没有找到匹配的文件或文件夹。"; return true; }
        reply = L"文件结果：";
        for (const auto& item : results) reply += L"\r\n- " + item.target;
        return true;
    }
    if (lower.starts_with(L"/open ")) {
        const auto query = Trim(input.substr(6));
        if (query.empty()) { reply = L"用法：/open <应用名>"; return true; }
        AppSearch apps;
        apps.BuildIndex();
        const auto results = apps.Query(query, 12);
        std::vector<SearchResult> exact;
        const auto expected = Lower(query);
        for (const auto& item : results) if (Lower(item.title) == expected) exact.push_back(item);
        if (exact.size() != 1) {
            reply = exact.empty() ? L"没有唯一精确匹配的应用，未执行打开。" : L"存在多个同名应用，未执行打开。请使用更精确名称。";
            return true;
        }
        const auto targetLower = Lower(fs::path(exact[0].target).filename().wstring());
        if (targetLower == L"cmd.exe" || targetLower == L"powershell.exe" || targetLower == L"pwsh.exe") {
            reply = L"L3 不启动命令解释器。需要终端或任意命令时请显式进入 L4。";
            return true;
        }
        const auto launched = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exact[0].target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        reply = launched > 32 ? L"已打开：" + exact[0].title : L"打开失败，ShellExecute 错误=" + std::to_wstring(launched);
        return true;
    }
    if (lower.starts_with(L"/open-file ")) {
        const auto query = Trim(input.substr(11));
        if (query.empty()) { reply = L"用法：/open-file <文件名>"; return true; }
        GozSearch files;
        if (!files.Available()) { reply = L"文件索引当前未就绪，未执行打开。"; return true; }
        const auto results = files.QuerySync(query, 20);
        std::vector<SearchResult> exact;
        const auto expected = Lower(query);
        for (const auto& item : results) if (Lower(item.title) == expected || Lower(item.target) == expected) exact.push_back(item);
        if (exact.size() != 1) {
            reply = exact.empty() ? L"没有唯一精确匹配的文件，未执行打开。" : L"存在多个同名文件，未执行打开。请使用完整路径。";
            return true;
        }
        const auto launched = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exact[0].target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        reply = launched > 32 ? L"已打开：" + exact[0].target : L"打开失败，ShellExecute 错误=" + std::to_wstring(launched);
        return true;
    }
'''
        text = text.replace(anchor, tools + anchor, 1)
    source.write_text(text, encoding="utf-8", newline="\n")


def patch_self_test() -> None:
    source = Path("src/native/src/main.cpp")
    text = source.read_text(encoding="utf-8")
    old = (
        '    if (reply.find(L"/status") == std::wstring::npos || reply.find(L"/time") == std::wstring::npos ||\n'
        '        reply.find(L"/new") == std::wstring::npos || reply.find(L"Ctrl+Enter") == std::wstring::npos) return false;\n'
    )
    new = (
        '    if (reply.find(L"/status") == std::wstring::npos || reply.find(L"/time") == std::wstring::npos ||\n'
        '        reply.find(L"/apps") == std::wstring::npos || reply.find(L"/files") == std::wstring::npos ||\n'
        '        reply.find(L"/open") == std::wstring::npos || reply.find(L"/open-file") == std::wstring::npos ||\n'
        '        reply.find(L"/new") == std::wstring::npos) return false;\n'
    )
    if old in text:
        text = text.replace(old, new, 1)
    require(text, 'reply.find(L"/apps")', "L3 help self-test")

    anchor = '    for (const wchar_t* command : {L"/new", L"/new-chat", L"新对话"}) {\n'
    if "/apps Notepad" not in text:
        require(text, anchor, "L3 local command self-test anchor")
        extra = (
            '    for (const wchar_t* command : {L"/apps Notepad", L"/files TuringDesk"}) {\n'
            '        reply.clear();\n'
            '        consumedSecret = false;\n'
            '        if (!l3.TryHandleLocal(command, reply, consumedSecret) || reply.empty() || consumedSecret) return false;\n'
            '    }\n\n'
        )
        text = text.replace(anchor, extra + anchor, 1)
    source.write_text(text, encoding="utf-8", newline="\n")


def verify_outputs() -> None:
    checks = {
        "src/native/include/turingdesk/GozSearch.h": ["QuerySync("],
        "src/native/src/GozSearch.cpp": ["GozSearch::QuerySync("],
        "src/native/src/L3Agent.cpp": [
            'lower.starts_with(L"/apps ")',
            'lower.starts_with(L"/files ")',
            'lower.starts_with(L"/open ")',
            'lower.starts_with(L"/open-file ")',
        ],
        "src/native/src/main.cpp": ["/apps Notepad"],
    }
    for path, markers in checks.items():
        text = Path(path).read_text(encoding="utf-8")
        for marker in markers:
            require(text, marker, f"output marker in {path}")


if __name__ == "__main__":
    patch_goz()
    patch_agent()
    patch_self_test()
    verify_outputs()
    print("L3 local tools migration applied successfully")
