#pragma once

// WASAPI loopback capture for the Scene input bus (B-2 / G1).
//
// Everything below the capture (downmix, resample, FFT, banding, beat detection) is
// platform-independent and lives in src/content/input/. This class is only the part
// that must touch Windows: pull interleaved float PCM off the default render device's
// loopback stream and hand it to that pipeline.
//
// Two rules shaped the design:
//
// 1. The analysis runs on the capture thread, not the render thread. A render frame is
//    16 ms and must never block on an audio packet; the render thread only copies one
//    already-analysed frame. That copy is the only synchronisation point.
// 2. Loopback produces silence when nothing is playing, and that silence must read as
//    silence — so the capture thread never zero-pads a short window to force a frame
//    out (FeedAnalyzer already leaves partial windows for the next call).
//
// Device loss is normal: unplugging a USB headset or switching the default output
// makes the render client fail mid-stream. Rather than surfacing that as a hard
// error, the tap stops, waits, and re-enumerates — the host keeps running.

#include "miaodesk/MiaoAudioCapture.h"

#include <memory>
#include <string>

namespace miaodesk::wallpaper {

class MiaoWallpaperAudioTap {
public:
    MiaoWallpaperAudioTap();
    ~MiaoWallpaperAudioTap();

    MiaoWallpaperAudioTap(const MiaoWallpaperAudioTap&) = delete;
    MiaoWallpaperAudioTap& operator=(const MiaoWallpaperAudioTap&) = delete;

    // Spawns the capture thread. Idempotent: a second call while running is a no-op
    // that returns true.
    //
    // Returns false only when the thread itself could not be created — there is no
    // other synchronous failure, because a machine with no audio endpoint is not an
    // error. That case is reported asynchronously through LastErrorText() and the
    // absence of frames, and the thread keeps retrying on a 400 ms backoff. The
    // implementation always returned true, while this comment used to promise false
    // when "the capture client could not be created" — a caller that trusted it would
    // have swallowed the reason a user's audio wallpaper sat still, because nothing
    // was ever going to set the return value.
    bool Start();

    // Signals the capture thread and joins it. Safe to call when not running.
    void Stop();

    bool Running() const noexcept;

    // Copies the most recent complete band/spectrum analysis. Returns false until the
    // first frame arrives (which takes one FFT window, ~43 ms at the default config).
    bool LatestFrame(content::inputbus::AudioSpectrumFrame* out) const;

    std::wstring LastErrorText() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace miaodesk::wallpaper
