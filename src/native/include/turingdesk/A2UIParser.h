#pragma once

#include <string>
#include <string_view>

namespace turingdesk::a2ui {

struct ValidationResult {
    bool success{};
    std::string normalizedJson;
    std::wstring message;
};

// Strictly validates the model-produced widget document. Unknown fields,
// executable-code surfaces and values outside the product limits are rejected.
// On success normalizedJson contains a canonical JSON representation safe to
// pass as data to the trusted WebView2 renderer via PostWebMessageAsJson.
ValidationResult ValidateWidgetDocument(std::string_view json);

// Product-owned examples. They use the same parser and sandbox path as AI output.
std::string BuiltInWidgetExample(std::string_view key);
std::string BuiltInWidgetExampleCatalogJson();

} // namespace turingdesk::a2ui
