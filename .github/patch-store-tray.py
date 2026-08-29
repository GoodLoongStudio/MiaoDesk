from pathlib import Path

path = Path('src/native/src/ui/search/SearchWindow.cpp')
text = path.read_text(encoding='utf-8')

anchor = 'void DrawSearchGlyph(ID2D1RenderTarget* target, ID2D1Brush* brush) {'
helper = r'''HICON LoadMiaoMiaoTrayIcon() {
    wchar_t exePath[32768]{};
    const DWORD length = GetModuleFileNameW(
        nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    if (length == 0 || length >= std::size(exePath)) return nullptr;

    const fs::path iconPath = fs::path(exePath).parent_path() / L"Assets" / L"MiaoMiao.ico";
    std::error_code ec;
    if (!fs::is_regular_file(iconPath, ec)) return nullptr;

    return reinterpret_cast<HICON>(LoadImageW(
        nullptr, iconPath.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_LOADFROMFILE));
}

'''
if 'HICON LoadMiaoMiaoTrayIcon()' not in text:
    if anchor not in text:
        raise SystemExit('DrawSearchGlyph anchor not found')
    text = text.replace(anchor, helper + anchor, 1)

old = '''    tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);\n    wcscpy_s(tray_.szTip, L"妙喵");\n    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;'''
new = '''    HICON packagedIcon = LoadMiaoMiaoTrayIcon();\n    tray_.hIcon = packagedIcon ? packagedIcon : LoadIconW(nullptr, IDI_APPLICATION);\n    wcscpy_s(tray_.szTip, L"妙喵");\n    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;\n    if (packagedIcon) DestroyIcon(packagedIcon);'''
if old not in text:
    if new not in text:
        raise SystemExit('tray icon block not found')
else:
    text = text.replace(old, new, 1)

path.write_text(text, encoding='utf-8')
