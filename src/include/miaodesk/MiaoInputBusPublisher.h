#pragma once

#include "miaodesk/MiaoInputBus.h"
#include "miaodesk/MiaoSceneRuntime.h"

#include <string>
#include <string_view>
#include <vector>

namespace miaodesk::content {

// Writes analysed audio and pointer state into a scene's InputBus.
//
// The scene declares the channels it consumes; the publisher only ever writes those.
// That single rule is what keeps a channel contract honest: a producer cannot
// introduce a value the scene never asked for, and a scene cannot record a channel
// the product does not define.
//
// Returns the channels it wrote so a host can decide whether another frame is
// needed without diffing the whole property store.
class InputBusPublisher {
public:
    struct Written {
        std::vector<std::wstring> ids;
        bool any{};
    };

    explicit InputBusPublisher(MiaoSceneRuntime& runtime) : runtime_(runtime) {}

    // Audio spectrum from AudioSpectrumAnalyzer::Analyze. Writes the bands, the
    // spectrum bins and the overall level for whichever of those the scene declares.
    Written PublishAudio(const inputbus::AudioSpectrumFrame& frame) {
        Written written;
        WriteFloat(written, inputbus::kAudioLevel, frame.level);
        WriteFloat(written, inputbus::kAudioBass, frame.bands[0]);
        WriteFloat(written, inputbus::kAudioLowMid, frame.bands[1]);
        WriteFloat(written, inputbus::kAudioMid, frame.bands[2]);
        WriteFloat(written, inputbus::kAudioHighMid, frame.bands[3]);
        WriteFloat(written, inputbus::kAudioTreble, frame.bands[4]);
        // The beat channel is an edge: writing it true only on the onset frame is
        // what lets AnimationTriggerMode::InputRisingEdge work. Writing a level here
        // instead would make every frame a rising edge after silence.
        if (frame.beat) {
            if (runtime_.SetInput(inputbus::kAudioBeat, true, &error_)) {
                written.ids.emplace_back(std::wstring(inputbus::kAudioBeat));
                written.any = true;
            }
        } else if (runtime_.GetInput(inputbus::kAudioBeat)) {
            // Clear the previous frame's true so the next onset is a real edge.
            if (runtime_.SetInput(inputbus::kAudioBeat, false, &error_)) {
                written.ids.emplace_back(std::wstring(inputbus::kAudioBeat));
                written.any = true;
            }
        }
        return written;
    }

    // Pointer state from PointerNormalizer::Apply. `interactive` says whether this
    // wallpaper has opted out of click-through; when it has not, the press/click
    // channels are withheld rather than synthesized as false.
    Written PublishPointer(const inputbus::PointerNormalizer::Result& pointer, bool interactive) {
        Written written;
        WriteFloat(written, inputbus::kPointerX, pointer.x);
        WriteFloat(written, inputbus::kPointerY, pointer.y);
        WriteBool(written, inputbus::kPointerInside, pointer.inside);
        if (interactive) {
            WriteBool(written, inputbus::kPointerDown, pointer.down);
            if (pointer.clicked) WriteBool(written, inputbus::kPointerClick, true);
            if (pointer.entered) WriteBool(written, inputbus::kPointerEnter, true);
            if (pointer.left) WriteBool(written, inputbus::kPointerLeave, true);
            // Non-firing edge channels are reset so the next transition is an edge.
            if (!pointer.clicked) WriteBool(written, inputbus::kPointerClick, false);
            if (!pointer.entered) WriteBool(written, inputbus::kPointerEnter, false);
            if (!pointer.left) WriteBool(written, inputbus::kPointerLeave, false);
        }
        return written;
    }

    const std::wstring& LastError() const noexcept { return error_; }

private:
    void WriteFloat(Written& written, std::wstring_view id, double value) {
        if (!runtime_.GetInput(id)) return;
        if (runtime_.SetInput(id, value, &error_)) {
            written.ids.emplace_back(std::wstring(id));
            written.any = true;
        }
    }

    void WriteBool(Written& written, std::wstring_view id, bool value) {
        if (!runtime_.GetInput(id)) return;
        if (runtime_.SetInput(id, value, &error_)) {
            written.ids.emplace_back(std::wstring(id));
            written.any = true;
        }
    }

    MiaoSceneRuntime& runtime_;
    std::wstring error_;
};

} // namespace miaodesk::content
