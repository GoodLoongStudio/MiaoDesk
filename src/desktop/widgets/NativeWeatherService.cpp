#include "miaodesk/NativeWeatherService.h"
#include "miaodesk/AppPaths.h"

#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace miaodesk::wallpaper {
namespace {

constexpr std::chrono::minutes kWeatherRefreshInterval{15};
constexpr std::chrono::seconds kWeatherRetryInterval{90};
constexpr std::size_t kMaxResponseBytes = 1024 * 1024;

fs::path WeatherCachePath() {
    const fs::path directory = paths::EnsureStateRoot();
    return directory.empty() ? fs::path{} : directory / L"weather-cache.ini";
}

std::wstring ReadProfile(const fs::path& path, const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> value(4096);
    GetPrivateProfileStringW(L"Weather", key, fallback, value.data(), static_cast<DWORD>(value.size()), path.c_str());
    return value.data();
}

bool WriteProfile(const fs::path& path, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(L"Weather", key, value.c_str(), path.c_str()) != FALSE;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length) <= 0)
        return {};
    return result;
}

void AppendUtf8Codepoint(std::string& out, unsigned codepoint) {
    if (codepoint <= 0x7F) out.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return -1;
}

std::optional<std::string> ExtractJsonString(const std::string& json, std::string_view key, std::size_t start = 0) {
    const std::size_t keyPos = json.find(key, start);
    if (keyPos == std::string::npos) return std::nullopt;
    const std::size_t colon = json.find(':', keyPos + key.size());
    if (colon == std::string::npos) return std::nullopt;
    std::size_t cursor = json.find('"', colon + 1);
    if (cursor == std::string::npos) return std::nullopt;
    ++cursor;
    std::string result;
    for (; cursor < json.size(); ++cursor) {
        const char ch = json[cursor];
        if (ch == '"') return result;
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (++cursor >= json.size()) break;
        switch (json[cursor]) {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u': {
            if (cursor + 4 >= json.size()) return std::nullopt;
            unsigned codepoint = 0;
            for (int i = 0; i < 4; ++i) {
                const int digit = HexDigit(json[cursor + 1 + static_cast<std::size_t>(i)]);
                if (digit < 0) return std::nullopt;
                codepoint = (codepoint << 4) | static_cast<unsigned>(digit);
            }
            cursor += 4;
            AppendUtf8Codepoint(result, codepoint);
            break;
        }
        default: result.push_back(json[cursor]); break;
        }
    }
    return std::nullopt;
}

std::optional<double> ExtractJsonNumber(const std::string& json, std::string_view key,
                                        std::size_t start = 0, std::size_t limit = std::string::npos) {
    const std::size_t keyPos = json.find(key, start);
    if (keyPos == std::string::npos || (limit != std::string::npos && keyPos >= limit)) return std::nullopt;
    const std::size_t colon = json.find(':', keyPos + key.size());
    if (colon == std::string::npos || (limit != std::string::npos && colon >= limit)) return std::nullopt;
    const char* begin = json.c_str() + colon + 1;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) return std::nullopt;
    return value;
}

std::vector<double> ExtractJsonNumberArray(const std::string& json, std::string_view key, std::size_t start) {
    std::vector<double> result;
    const std::size_t keyPos = json.find(key, start);
    if (keyPos == std::string::npos) return result;
    const std::size_t open = json.find('[', keyPos + key.size());
    if (open == std::string::npos) return result;
    const std::size_t close = json.find(']', open + 1);
    if (close == std::string::npos) return result;
    const char* cursor = json.c_str() + open + 1;
    const char* finish = json.c_str() + close;
    while (cursor < finish) {
        while (cursor < finish && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' || *cursor == ',')) ++cursor;
        if (cursor >= finish) break;
        char* end = nullptr;
        const double value = std::strtod(cursor, &end);
        if (end == cursor || end > finish) break;
        result.push_back(value);
        cursor = end;
    }
    return result;
}

std::vector<std::string> ExtractJsonStringArray(const std::string& json, std::string_view key, std::size_t start) {
    std::vector<std::string> result;
    const std::size_t keyPos = json.find(key, start);
    if (keyPos == std::string::npos) return result;
    const std::size_t open = json.find('[', keyPos + key.size());
    if (open == std::string::npos) return result;
    const std::size_t close = json.find(']', open + 1);
    if (close == std::string::npos) return result;
    std::size_t cursor = open + 1;
    while (cursor < close) {
        cursor = json.find('"', cursor);
        if (cursor == std::string::npos || cursor >= close) break;
        const std::size_t valueStart = cursor + 1;
        std::size_t end = valueStart;
        bool escaped = false;
        for (; end < close; ++end) {
            if (!escaped && json[end] == '"') break;
            if (!escaped && json[end] == '\\') escaped = true;
            else escaped = false;
        }
        if (end >= close) break;
        result.push_back(json.substr(valueStart, end - valueStart));
        cursor = end + 1;
    }
    return result;
}

std::wstring WeatherCondition(int code) {
    switch (code) {
    case 0: return L"晴";
    case 1: return L"晴间多云";
    case 2: return L"多云";
    case 3: return L"阴";
    case 45: case 48: return L"雾";
    case 51: return L"小毛毛雨";
    case 53: return L"毛毛雨";
    case 55: return L"强毛毛雨";
    case 56: case 57: return L"冻毛毛雨";
    case 61: return L"小雨";
    case 63: return L"中雨";
    case 65: return L"大雨";
    case 66: case 67: return L"冻雨";
    case 71: return L"小雪";
    case 73: return L"中雪";
    case 75: return L"大雪";
    case 77: return L"雪粒";
    case 80: return L"小阵雨";
    case 81: return L"阵雨";
    case 82: return L"强阵雨";
    case 85: case 86: return L"阵雪";
    case 95: return L"雷暴";
    case 96: case 99: return L"雷暴伴冰雹";
    default: return L"天气变化";
    }
}

bool HttpGet(std::wstring_view host, std::wstring_view path, std::string* body, std::wstring* error) {
    if (!body) return false;
    body->clear();
    HINTERNET session = WinHttpOpen(L"MiaoMiao-NativeWeather/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        if (error) *error = L"WinHTTP session failed";
        return false;
    }
    WinHttpSetTimeouts(session, 4000, 4000, 5000, 7000);
    HINTERNET connection = WinHttpConnect(session, std::wstring(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        if (error) *error = L"WinHTTP connect failed";
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", std::wstring(path).c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    bool ok = request != nullptr;
    if (ok) {
        const wchar_t headers[] = L"Accept: application/json\r\n";
        WinHttpAddRequestHeaders(request, headers, static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);
        ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE &&
             WinHttpReceiveResponse(request, nullptr) != FALSE;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (ok) {
        ok = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) != FALSE &&
             status >= 200 && status < 300;
    }
    while (ok && body->size() < kMaxResponseBytes) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) { ok = false; break; }
        if (available == 0) break;
        const std::size_t remaining = kMaxResponseBytes - body->size();
        const DWORD take = static_cast<DWORD>(std::min<std::size_t>(available, remaining));
        const std::size_t offset = body->size();
        body->resize(offset + take);
        DWORD read = 0;
        if (!WinHttpReadData(request, body->data() + offset, take, &read)) { ok = false; break; }
        body->resize(offset + read);
        if (read == 0) break;
    }
    if (!ok && error && error->empty()) *error = L"HTTPS request failed";
    if (request) WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok && !body->empty();
}

bool ResolveApproximateLocation(double* latitude, double* longitude, std::wstring* label, std::wstring* error) {
    std::string body;
    if (!HttpGet(L"ipwho.is", L"/", &body, error)) return false;
    const auto lat = ExtractJsonNumber(body, "\"latitude\"");
    const auto lon = ExtractJsonNumber(body, "\"longitude\"");
    const auto city = ExtractJsonString(body, "\"city\"");
    if (!lat || !lon || *lat < -90.0 || *lat > 90.0 || *lon < -180.0 || *lon > 180.0) {
        if (error) *error = L"IP location response missing coordinates";
        return false;
    }
    *latitude = *lat;
    *longitude = *lon;
    if (label) {
        *label = city ? Utf8ToWide(*city) : L"本地";
        if (label->empty()) *label = L"本地";
    }
    return true;
}

bool FetchOpenMeteo(double latitude, double longitude, std::wstring location,
                    NativeWeatherSnapshot* snapshot, std::wstring* error) {
    if (!snapshot) return false;
    std::wostringstream path;
    path.imbue(std::locale::classic());
    path << L"/v1/forecast?latitude=" << std::fixed << std::setprecision(5) << latitude
         << L"&longitude=" << std::fixed << std::setprecision(5) << longitude
         << L"&current=temperature_2m,weather_code"
         << L"&hourly=temperature_2m,weather_code"
         << L"&daily=temperature_2m_max,temperature_2m_min"
         << L"&timezone=auto&forecast_days=2";

    std::string body;
    if (!HttpGet(L"api.open-meteo.com", path.str(), &body, error)) return false;
    const std::size_t currentPos = body.find("\"current\":");
    const std::size_t hourlyPos = body.find("\"hourly\":");
    const std::size_t dailyPos = body.find("\"daily\":");
    if (currentPos == std::string::npos || hourlyPos == std::string::npos || dailyPos == std::string::npos) {
        if (error) *error = L"Open-Meteo response missing weather blocks";
        return false;
    }
    const auto currentTime = ExtractJsonString(body, "\"time\"", currentPos);
    const auto currentTemp = ExtractJsonNumber(body, "\"temperature_2m\"", currentPos, hourlyPos);
    const auto currentCode = ExtractJsonNumber(body, "\"weather_code\"", currentPos, hourlyPos);
    const auto hourlyTimes = ExtractJsonStringArray(body, "\"time\"", hourlyPos);
    const auto hourlyTemps = ExtractJsonNumberArray(body, "\"temperature_2m\"", hourlyPos);
    const auto hourlyCodes = ExtractJsonNumberArray(body, "\"weather_code\"", hourlyPos);
    const auto highs = ExtractJsonNumberArray(body, "\"temperature_2m_max\"", dailyPos);
    const auto lows = ExtractJsonNumberArray(body, "\"temperature_2m_min\"", dailyPos);
    if (!currentTime || !currentTemp || !currentCode || hourlyTimes.empty() || hourlyTemps.empty() || highs.empty() || lows.empty()) {
        if (error) *error = L"Open-Meteo response missing required values";
        return false;
    }

    NativeWeatherSnapshot next;
    next.valid = true;
    next.location = location.empty() ? L"本地" : std::move(location);
    next.latitude = latitude;
    next.longitude = longitude;
    next.temperatureC = static_cast<int>(std::lround(*currentTemp));
    next.weatherCode = static_cast<int>(std::lround(*currentCode));
    next.condition = WeatherCondition(next.weatherCode);
    next.highC = static_cast<int>(std::lround(highs.front()));
    next.lowC = static_cast<int>(std::lround(lows.front()));
    next.observedTime = Utf8ToWide(*currentTime);
    next.status = L"Open-Meteo · 真实天气";

    std::size_t firstHour = 0;
    if (currentTime->size() >= 13) {
        const std::string hourKey = currentTime->substr(0, 13) + ":00";
        const auto found = std::find(hourlyTimes.begin(), hourlyTimes.end(), hourKey);
        if (found != hourlyTimes.end()) firstHour = static_cast<std::size_t>(found - hourlyTimes.begin());
    }
    for (std::size_t i = 0; i < next.hours.size(); ++i) {
        const std::size_t index = std::min(firstHour + i, hourlyTemps.size() - 1);
        if (index < hourlyTimes.size()) {
            const std::string& value = hourlyTimes[index];
            next.hours[i].label = value.size() >= 16 ? Utf8ToWide(std::string_view(value).substr(11, 5)) : L"--:--";
        } else {
            next.hours[i].label = L"--:--";
        }
        next.hours[i].temperatureC = static_cast<int>(std::lround(hourlyTemps[index]));
        if (index < hourlyCodes.size()) next.hours[i].weatherCode = static_cast<int>(std::lround(hourlyCodes[index]));
    }
    *snapshot = std::move(next);
    return true;
}

bool EnsureUnicodeCacheFile(const fs::path& path) {
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
    if (!EnsureUnicodeCacheFile(path)) return;
    bool ok = true;
    ok = WriteProfile(path, L"Valid", L"1") && ok;
    ok = WriteProfile(path, L"Location", snapshot.location) && ok;
    ok = WriteProfile(path, L"Latitude", std::to_wstring(snapshot.latitude)) && ok;
    ok = WriteProfile(path, L"Longitude", std::to_wstring(snapshot.longitude)) && ok;
    ok = WriteProfile(path, L"Temperature", std::to_wstring(snapshot.temperatureC)) && ok;
    ok = WriteProfile(path, L"High", std::to_wstring(snapshot.highC)) && ok;
    ok = WriteProfile(path, L"Low", std::to_wstring(snapshot.lowC)) && ok;
    ok = WriteProfile(path, L"Code", std::to_wstring(snapshot.weatherCode)) && ok;
    ok = WriteProfile(path, L"Condition", snapshot.condition) && ok;
    ok = WriteProfile(path, L"ObservedTime", snapshot.observedTime) && ok;
    ok = WriteProfile(path, L"Status", snapshot.status) && ok;
    for (std::size_t i = 0; i < snapshot.hours.size(); ++i) {
        const std::wstring suffix = std::to_wstring(i);
        ok = WriteProfile(path, (L"Hour" + suffix + L"Label").c_str(), snapshot.hours[i].label) && ok;
        ok = WriteProfile(path, (L"Hour" + suffix + L"Temp").c_str(), std::to_wstring(snapshot.hours[i].temperatureC)) && ok;
        ok = WriteProfile(path, (L"Hour" + suffix + L"Code").c_str(), std::to_wstring(snapshot.hours[i].weatherCode)) && ok;
    }
    if (ok) WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
}

} // namespace

struct NativeWeatherService::Impl {
    mutable std::mutex mutex;
    NativeWeatherSnapshot snapshot;
    std::jthread worker;
    HWND notifyWindow{};
    UINT notifyMessage{};

    void Publish(NativeWeatherSnapshot value) {
        {
            std::scoped_lock lock(mutex);
            snapshot = std::move(value);
        }
        if (notifyWindow && IsWindow(notifyWindow) && notifyMessage != 0) PostMessageW(notifyWindow, notifyMessage, 0, 0);
    }

    NativeWeatherSnapshot Copy() const {
        std::scoped_lock lock(mutex);
        return snapshot;
    }

    void Run(std::stop_token stopToken) {
        NativeWeatherSnapshot cached;
        if (NativeWeatherService::ReadCachedSnapshot(&cached)) Publish(cached);

        double latitude = cached.latitude;
        double longitude = cached.longitude;
        std::wstring location = cached.location;
        bool haveLocation = false;
        std::wstring geoError;
        double resolvedLat = 0.0;
        double resolvedLon = 0.0;
        std::wstring resolvedLocation;
        if (ResolveApproximateLocation(&resolvedLat, &resolvedLon, &resolvedLocation, &geoError)) {
            latitude = resolvedLat;
            longitude = resolvedLon;
            location = std::move(resolvedLocation);
            haveLocation = true;
        } else if (cached.valid && cached.latitude >= -90.0 && cached.latitude <= 90.0 &&
                   cached.longitude >= -180.0 && cached.longitude <= 180.0) {
            haveLocation = true;
        }

        while (!stopToken.stop_requested()) {
            bool success = false;
            if (haveLocation) {
                NativeWeatherSnapshot fresh;
                std::wstring fetchError;
                if (FetchOpenMeteo(latitude, longitude, location, &fresh, &fetchError)) {
                    SaveCachedSnapshot(fresh);
                    Publish(std::move(fresh));
                    success = true;
                } else {
                    NativeWeatherSnapshot fallback = Copy();
                    if (!fallback.valid) {
                        fallback.status = fetchError.empty() ? L"天气网络暂不可用" : fetchError;
                        Publish(std::move(fallback));
                    }
                }
            } else {
                NativeWeatherSnapshot unavailable = Copy();
                if (!unavailable.valid) {
                    unavailable.status = geoError.empty() ? L"无法定位天气位置" : geoError;
                    Publish(std::move(unavailable));
                }
            }

            const auto wait = success ? kWeatherRefreshInterval : kWeatherRetryInterval;
            const auto deadline = std::chrono::steady_clock::now() + wait;
            while (!stopToken.stop_requested() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

NativeWeatherService::NativeWeatherService() : impl_(std::make_unique<Impl>()) {}
NativeWeatherService::~NativeWeatherService() { Stop(); }

void NativeWeatherService::Start(HWND notifyWindow, UINT notifyMessage) {
    Stop();
    impl_->notifyWindow = notifyWindow;
    impl_->notifyMessage = notifyMessage;
    impl_->worker = std::jthread([this](std::stop_token stopToken) { impl_->Run(stopToken); });
}

void NativeWeatherService::Stop() {
    if (!impl_) return;
    if (impl_->worker.joinable()) {
        impl_->worker.request_stop();
        impl_->worker.join();
    }
    impl_->notifyWindow = nullptr;
    impl_->notifyMessage = 0;
}

NativeWeatherSnapshot NativeWeatherService::Snapshot() const {
    return impl_ ? impl_->Copy() : NativeWeatherSnapshot{};
}

bool NativeWeatherService::ReadCachedSnapshot(NativeWeatherSnapshot* snapshot) {
    if (!snapshot) return false;
    const fs::path path = WeatherCachePath();
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    if (_wtoi(ReadProfile(path, L"Valid", L"0").c_str()) == 0) return false;

    NativeWeatherSnapshot value;
    value.valid = true;
    value.location = ReadProfile(path, L"Location", L"本地");
    value.latitude = std::wcstod(ReadProfile(path, L"Latitude", L"0").c_str(), nullptr);
    value.longitude = std::wcstod(ReadProfile(path, L"Longitude", L"0").c_str(), nullptr);
    value.temperatureC = _wtoi(ReadProfile(path, L"Temperature", L"0").c_str());
    value.highC = _wtoi(ReadProfile(path, L"High", L"0").c_str());
    value.lowC = _wtoi(ReadProfile(path, L"Low", L"0").c_str());
    value.weatherCode = _wtoi(ReadProfile(path, L"Code", L"0").c_str());
    value.condition = ReadProfile(path, L"Condition", WeatherCondition(value.weatherCode).c_str());
    value.observedTime = ReadProfile(path, L"ObservedTime", L"");
    value.status = ReadProfile(path, L"Status", L"Open-Meteo · 缓存天气");
    for (std::size_t i = 0; i < value.hours.size(); ++i) {
        const std::wstring suffix = std::to_wstring(i);
        value.hours[i].label = ReadProfile(path, (L"Hour" + suffix + L"Label").c_str(), L"--:--");
        value.hours[i].temperatureC = _wtoi(ReadProfile(path, (L"Hour" + suffix + L"Temp").c_str(), L"0").c_str());
        value.hours[i].weatherCode = _wtoi(ReadProfile(path, (L"Hour" + suffix + L"Code").c_str(), L"0").c_str());
    }
    *snapshot = std::move(value);
    return true;
}

bool NativeWeatherService::SelfTest() noexcept {
    const std::string sample = R"({"current":{"time":"2026-08-29T16:15","temperature_2m":21.6,"weather_code":2},"hourly":{"time":["2026-08-29T16:00","2026-08-29T17:00"],"temperature_2m":[21.6,20.8],"weather_code":[2,3]},"daily":{"temperature_2m_max":[24.1],"temperature_2m_min":[15.2]}})";
    const std::size_t current = sample.find("\"current\":");
    const std::size_t hourly = sample.find("\"hourly\":");
    const auto temp = ExtractJsonNumber(sample, "\"temperature_2m\"", current, hourly);
    const auto code = ExtractJsonNumber(sample, "\"weather_code\"", current, hourly);
    const auto times = ExtractJsonStringArray(sample, "\"time\"", hourly);
    return temp && std::lround(*temp) == 22 && code && std::lround(*code) == 2 &&
           times.size() == 2 && WeatherCondition(2) == L"多云";
}

} // namespace miaodesk::wallpaper
