#pragma once

#include <array>
#include <cstddef>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

#include "miaodesk/MiaoInputBus.h"

namespace miaodesk::wallpaper {

// Builds the JSON envelope that WallpaperWebAudioBridge.js's normalizeFrame accepts.
//
// This exists as its own pure function rather than inline at the postMessage call site
// for one reason: the contract between host and page lives entirely in field names,
// array lengths and value ranges, and that is the part most likely to drift. Keeping it
// here means it can be tested on every machine (see tests/WebAudioEnvelope.cpp) instead
// of only wherever the WebView2 surface happens to run, and the node-side shim test
// (tests/WebAudioBridge.mjs) checks the two sides against each other.
//
// Three decisions worth stating, because each one is a bug that has already happened or
// that "looks fine" until a particular machine sees it:
//
//   1. **Classic locale, explicitly.** std::ostringstream uses the global locale, so on
//      a machine whose locale formats decimals with a comma this would emit
//      `"level":0,5` and the page's JSON.parse would throw — on that machine only.
//      Imbuing std::locale::classic() pins the decimal point to '.' everywhere.
//
//   2. **Fixed precision, not shortest round-trip.** Four decimals is well below the
//      8-bit quantisation any visualisation will apply, and fixed notation keeps the
//      output byte-identical across platforms. The default `<<` for a double emits up
//      to 17 significant digits, which is both noise on the wire and a source of
//      last-digit differences between compilers' float formatting.
//
//   3. **Clamp and de-NaN here, mirroring the shim's unit().** The shim coerces rather
//      than rejects individual values, so a host sending NaN or 1.5 produces silence or
//      full scale silently. Doing the same coercion on this side means what the page
//      receives is what the analyzer actually produced, and a value out of range is a
//      host bug that shows up as the clamped number instead of a page-side mystery.
//      The analyzer already guarantees [0,1] and finiteness; this is the second line,
//      not the first.
inline std::string BuildAudioBridgeEnvelope(const content::inputbus::AudioSpectrumFrame& frame) {
    // The shim declares these two counts in JS. They are the whole shape of the frame:
    // numericArray() rejects a wrong-length array outright, so a count mismatch is not
    // a tolerance issue — it drops every frame on the floor with no error anywhere.
    // Asserted against the *types*, not against `frame.bands.size()`: a member of a
    // function parameter is not a constant expression, and the first version of this
    // line only compiled because g++ let it slide while clang correctly refused.
    using Frame = content::inputbus::AudioSpectrumFrame;
    static_assert(std::tuple_size<decltype(std::declval<const Frame&>().bands)>::value == 5,
                  "shim BAND_COUNT is 5");
    static_assert(content::inputbus::kSpectrumBins == 16,
                  "shim SPECTRUM_COUNT is 16");

    // Refuse to emit a frame the shim would silently drop. Cheaper to be wrong here,
    // where the failing line has a name, than on the page, where nothing reports.
    if (frame.bands.size() != 5) return {};
    if (frame.spectrum.size() != content::inputbus::kSpectrumBins) return {};

    auto unit = [](double value) {
        if (!(value >= 0.0)) return 0.0;  // false for NaN, and for < 0
        if (value > 1.0) return 1.0;
        return value;
    };
    auto number = [&unit](double value) {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        // setprecision under std::fixed means digits *after* the point. Four is far
        // below the resolution any consumer can act on and keeps the payload small;
        // the default 6 would be fine too, but not writing it at all would silently
        // fall back to 6 and make the comment above a lie.
        text << std::fixed << std::setprecision(4) << unit(value);
        return text.str();
    };

    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << "{\"type\":\"audio\",\"frame\":{\"level\":" << number(frame.level)
         << ",\"bands\":[";
    for (std::size_t i = 0; i < frame.bands.size(); ++i) {
        if (i != 0) json << ',';
        json << number(frame.bands[i]);
    }
    json << "],\"spectrum\":[";
    for (std::size_t i = 0; i < frame.spectrum.size(); ++i) {
        if (i != 0) json << ',';
        json << number(frame.spectrum[i]);
    }
    // A JSON boolean, not 0/1: the shim reads beat as an edge with `=== true`, so
    // sending 1 would make every frame a beat.
    json << "],\"beat\":" << (frame.beat ? "true" : "false") << "}}";
    return json.str();
}

} // namespace miaodesk::wallpaper
