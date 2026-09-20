#include "miaodesk/MiaoContentDataBinding.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace miaodesk::content {
namespace {

bool Fail(std::wstring* error, std::wstring message) {
    if (error) *error = std::move(message);
    return false;
}

bool HasCapability(const ContentDefinition& definition, std::wstring_view capability) {
    if (std::find(definition.capabilities.begin(), definition.capabilities.end(), capability) !=
        definition.capabilities.end()) return true;

    if (capability == L"clock.read") {
        return std::find(definition.capabilities.begin(), definition.capabilities.end(), L"clock") !=
               definition.capabilities.end();
    }
    if (capability == L"weather.read") {
        return std::find(definition.capabilities.begin(), definition.capabilities.end(), L"weather") !=
               definition.capabilities.end();
    }
    return false;
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::wstring TwoDigits(std::uint16_t value) {
    wchar_t text[8]{};
    swprintf_s(text, L"%02u", static_cast<unsigned>(value));
    return text;
}

std::wstring FourDigits(std::uint16_t value) {
    wchar_t text[8]{};
    swprintf_s(text, L"%04u", static_cast<unsigned>(value));
    return text;
}

std::wstring ToTemplateText(const ContentDataValue& value, bool* supported) {
    if (supported) *supported = true;
    if (const auto* text = std::get_if<std::wstring>(&value)) return *text;
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? L"true" : L"false";
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return std::to_wstring(*integer);
    if (const auto* number = std::get_if<double>(&value)) {
        if (!std::isfinite(*number)) {
            if (supported) *supported = false;
            return {};
        }
        std::wostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(12) << *number;
        return stream.str();
    }
    if (supported) *supported = false;
    return {};
}

std::wstring_view Trim(std::wstring_view value) noexcept {
    while (!value.empty() && (value.front() == L' ' || value.front() == L'\t' ||
                              value.front() == L'\r' || value.front() == L'\n')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t' ||
                              value.back() == L'\r' || value.back() == L'\n')) value.remove_suffix(1);
    return value;
}

bool ValidClockFields(
    std::uint16_t year,
    std::uint16_t month,
    std::uint16_t day,
    std::uint16_t dayOfWeek,
    std::uint16_t hour,
    std::uint16_t minute,
    std::uint16_t second) noexcept {
    return year >= 1601 && year <= 9999 && month >= 1 && month <= 12 &&
           day >= 1 && day <= 31 && dayOfWeek <= 6 && hour <= 23 && minute <= 59 && second <= 59;
}

} // namespace

std::optional<std::wstring_view> MiaoContentCapabilityBroker::RequiredCapability(
    std::wstring_view dataPath) noexcept {
    if (StartsWith(dataPath, L"time.")) return L"clock.read";
    if (StartsWith(dataPath, L"weather.")) return L"weather.read";
    return std::nullopt;
}

bool MiaoContentCapabilityBroker::CanRead(
    const ContentDefinition& definition,
    std::wstring_view dataPath,
    std::wstring* error) {
    if (error) error->clear();
    if (dataPath.empty()) return Fail(error, L"Content data path is empty.");
    const auto required = RequiredCapability(dataPath);
    if (!required)
        return Fail(error, L"Content data path is not supported by the capability broker: " + std::wstring(dataPath));
    if (!HasCapability(definition, *required))
        return Fail(error, L"Content did not declare required capability " + std::wstring(*required) +
                           L" for data path " + std::wstring(dataPath) + L".");
    return true;
}

bool MiaoContentCapabilityBroker::SelfTest() {
    ContentDefinition content;
    content.id = L"binding.selftest";
    content.name = L"Binding Self Test";
    content.version = L"1";
    content.entry = L"scene.json";
    content.capabilities = {L"clock.read"};
    std::wstring error;
    if (!CanRead(content, L"time.hhmm", &error)) return false;
    if (CanRead(content, L"weather.temperature", &error)) return false;
    content.capabilities.push_back(L"weather.read");
    if (!CanRead(content, L"weather.temperature", &error)) return false;
    content.capabilities = {L"weather"};
    if (!CanRead(content, L"weather.condition", &error)) return false;
    content.capabilities.clear();
    if (CanRead(content, L"time.hhmm", &error)) return false;
    content.capabilities = {L"clock"};
    return CanRead(content, L"time.hhmm", &error);
}

ContentDataSnapshot MiaoTimeDataProvider::CaptureLocalTime() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    return Capture(
        local.wYear, local.wMonth, local.wDay, local.wDayOfWeek,
        local.wHour, local.wMinute, local.wSecond,
        static_cast<std::uint64_t>(GetTickCount64()));
}

ContentDataSnapshot MiaoTimeDataProvider::Capture(
    std::uint16_t year,
    std::uint16_t month,
    std::uint16_t day,
    std::uint16_t dayOfWeek,
    std::uint16_t hour,
    std::uint16_t minute,
    std::uint16_t second,
    std::uint64_t generation) {
    ContentDataSnapshot snapshot;
    snapshot.generation = generation;
    if (!ValidClockFields(year, month, day, dayOfWeek, hour, minute, second)) return snapshot;

    snapshot.values.emplace(L"time.hhmm", TwoDigits(hour) + L":" + TwoDigits(minute));
    snapshot.values.emplace(L"time.hhmmss", TwoDigits(hour) + L":" + TwoDigits(minute) + L":" + TwoDigits(second));
    snapshot.values.emplace(L"time.date", FourDigits(year) + L"-" + TwoDigits(month) + L"-" + TwoDigits(day));
    snapshot.values.emplace(L"time.year", static_cast<std::int64_t>(year));
    snapshot.values.emplace(L"time.month", static_cast<std::int64_t>(month));
    snapshot.values.emplace(L"time.day", static_cast<std::int64_t>(day));
    snapshot.values.emplace(L"time.weekday", static_cast<std::int64_t>(dayOfWeek));
    snapshot.values.emplace(L"time.hour", static_cast<std::int64_t>(hour));
    snapshot.values.emplace(L"time.minute", static_cast<std::int64_t>(minute));
    snapshot.values.emplace(L"time.second", static_cast<std::int64_t>(second));
    return snapshot;
}

const ContentDataValue* MiaoContentDataBinding::Find(
    const ContentDefinition& definition,
    const ContentDataSnapshot& snapshot,
    std::wstring_view dataPath,
    std::wstring* error) {
    if (!MiaoContentCapabilityBroker::CanRead(definition, dataPath, error)) return nullptr;
    const auto found = snapshot.values.find(dataPath);
    if (found == snapshot.values.end()) {
        Fail(error, L"Content data value is unavailable: " + std::wstring(dataPath));
        return nullptr;
    }
    if (error) error->clear();
    return &found->second;
}

bool MiaoContentDataBinding::ResolveTemplate(
    const ContentDefinition& definition,
    const ContentDataSnapshot& snapshot,
    std::wstring_view source,
    std::wstring* resolved,
    std::wstring* error) {
    if (!resolved) return Fail(error, L"Resolved content template output is null.");
    if (source.size() > 16384) return Fail(error, L"Content binding template is too large.");

    std::wstring output;
    output.reserve(source.size() + 32);
    std::size_t cursor = 0;
    std::size_t substitutions = 0;
    while (cursor < source.size()) {
        const std::size_t open = source.find(L"{{", cursor);
        if (open == std::wstring_view::npos) {
            output.append(source.substr(cursor));
            break;
        }
        output.append(source.substr(cursor, open - cursor));
        const std::size_t close = source.find(L"}}", open + 2);
        if (close == std::wstring_view::npos)
            return Fail(error, L"Content binding template has an unterminated placeholder.");
        if (++substitutions > 64)
            return Fail(error, L"Content binding template has too many placeholders.");

        const std::wstring_view path = Trim(source.substr(open + 2, close - (open + 2)));
        if (path.empty()) return Fail(error, L"Content binding placeholder is empty.");
        const ContentDataValue* value = Find(definition, snapshot, path, error);
        if (!value) return false;
        bool textSupported = false;
        const std::wstring text = ToTemplateText(*value, &textSupported);
        if (!textSupported)
            return Fail(error, L"Content data value cannot be embedded in text: " + std::wstring(path));
        output += text;
        if (output.size() > 65536)
            return Fail(error, L"Resolved content binding template is too large.");
        cursor = close + 2;
    }

    *resolved = std::move(output);
    if (error) error->clear();
    return true;
}

bool MiaoContentDataBinding::SelfTest() {
    ContentDefinition clock;
    clock.id = L"builtin.glass-clock";
    clock.name = L"Glass Clock";
    clock.version = L"1.0.0";
    clock.entry = L"scene.json";
    clock.kind = ContentKind::Widget;
    clock.capabilities = {L"clock.read", L"weather.read"};

    auto snapshot = MiaoTimeDataProvider::Capture(2026, 9, 10, 4, 21, 7, 5, 42);
    if (snapshot.generation != 42 || snapshot.values.size() < 10) return false;
    snapshot.values.emplace(L"weather.temperature", static_cast<std::int64_t>(23));
    snapshot.values.emplace(L"weather.condition", std::wstring(L"晴"));

    std::wstring error;
    const auto* hhmm = Find(clock, snapshot, L"time.hhmm", &error);
    if (!hhmm || !std::holds_alternative<std::wstring>(*hhmm) ||
        std::get<std::wstring>(*hhmm) != L"21:07") return false;

    std::wstring resolved;
    if (!ResolveTemplate(clock, snapshot,
                         L"现在是 {{time.hhmm}} · {{ weather.temperature }}° · {{weather.condition}}",
                         &resolved, &error)) return false;
    if (resolved != L"现在是 21:07 · 23° · 晴") return false;

    ContentDefinition denied = clock;
    denied.capabilities = {L"clock.read"};
    if (ResolveTemplate(denied, snapshot, L"{{weather.temperature}}", &resolved, &error)) return false;
    denied.capabilities.clear();
    if (ResolveTemplate(denied, snapshot, L"{{time.hhmm}}", &resolved, &error)) return false;
    if (ResolveTemplate(clock, snapshot, L"{{time.hhmm", &resolved, &error)) return false;
    return MiaoContentCapabilityBroker::SelfTest();
}

} // namespace miaodesk::content
