from pathlib import Path

p = Path('src/native/src/desktop/widgets/NativeWeatherService.cpp')
s = p.read_text(encoding='utf-8')

old = '''void SaveCachedSnapshot(const NativeWeatherSnapshot& snapshot) {\n    if (!snapshot.valid) return;\n    const fs::path path = WeatherCachePath();'''
new = r'''bool EnsureUnicodeCacheFile(const fs::path& path) {
    bool unicode = false;
    HANDLE existing = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (existing != INVALID_HANDLE_VALUE) {
        unsigned char bom[2]{};
        DWORD read = 0;
        if (ReadFile(existing, bom, sizeof(bom), &read, nullptr) && read == sizeof(bom))
            unicode = bom[0] == 0xFF && bom[1] == 0xFE;
        CloseHandle(existing);
    }
    if (unicode) return true;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const unsigned char bom[2]{0xFF, 0xFE};
    DWORD written = 0;
    const bool ok = WriteFile(file, bom, sizeof(bom), &written, nullptr) != FALSE && written == sizeof(bom);
    FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

void SaveCachedSnapshot(const NativeWeatherSnapshot& snapshot) {
    if (!snapshot.valid) return;
    const fs::path path = WeatherCachePath();
    if (!EnsureUnicodeCacheFile(path)) return;'''
if old not in s:
    raise SystemExit('missing cache save anchor')
s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8')
print('weather cache hardening applied')
