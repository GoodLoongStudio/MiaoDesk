#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "miaodesk/MiaoContentModel.h"

namespace miaodesk::content {

using ContentDataValue = PropertyValue;
using ContentDataValues = std::map<std::wstring, ContentDataValue, std::less<>>;

struct ContentDataSnapshot {
    std::uint64_t generation{};
    ContentDataValues values;
};

class MiaoContentCapabilityBroker {
public:
    // Returns the capability required to read a declarative data path.
    // An empty result means that the path is not supported by this broker.
    static std::optional<std::wstring_view> RequiredCapability(std::wstring_view dataPath) noexcept;

    static bool CanRead(
        const ContentDefinition& definition,
        std::wstring_view dataPath,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

class MiaoTimeDataProvider {
public:
    // Captures the current local clock through the host. Content never receives
    // direct Windows API access; it only receives these bounded typed values.
    static ContentDataSnapshot CaptureLocalTime();

    // Deterministic variant used by tests and preview tooling.
    static ContentDataSnapshot Capture(
        std::uint16_t year,
        std::uint16_t month,
        std::uint16_t day,
        std::uint16_t dayOfWeek,
        std::uint16_t hour,
        std::uint16_t minute,
        std::uint16_t second,
        std::uint64_t generation = 1);
};

class MiaoContentDataBinding {
public:
    static const ContentDataValue* Find(
        const ContentDefinition& definition,
        const ContentDataSnapshot& snapshot,
        std::wstring_view dataPath,
        std::wstring* error = nullptr);

    // Resolves {{time.*}} style placeholders. Every referenced path is checked
    // through MiaoContentCapabilityBroker before data is exposed.
    static bool ResolveTemplate(
        const ContentDefinition& definition,
        const ContentDataSnapshot& snapshot,
        std::wstring_view source,
        std::wstring* resolved,
        std::wstring* error = nullptr);

    static bool SelfTest();
};

} // namespace miaodesk::content
