#pragma once

#include <string>
#include <string_view>

namespace miaodesk::widget_intent {

struct ComposedWidget {
    bool success{};
    std::wstring title;
    std::string a2uiJson;
    std::wstring message;
};

// True when the user is asking to add/generate a desktop widget in one sentence.
bool LooksLikeOneSentenceWidgetRequest(std::wstring_view prompt);

// Maps a natural-language widget request to a validated A2UI document using
// product-owned templates. This powers no-Key demo generation and gives the
// Pi agent a deterministic starting point for preview-first widget creation.
ComposedWidget ComposeFromPrompt(std::wstring_view prompt);

} // namespace miaodesk::widget_intent
