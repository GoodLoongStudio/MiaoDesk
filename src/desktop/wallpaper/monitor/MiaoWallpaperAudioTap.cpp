#include "miaodesk/MiaoWallpaperAudioTap.h"

#include <windows.h>
#include <audioclient.h>
#include <combaseapi.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objbase.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace miaodesk::wallpaper {
namespace {

using Microsoft::WRL::ComPtr;

constexpr REFERENCE_TIME kBufferHns = 10'000'000;  // 1 s; WASAPI units are 100 ns.
constexpr UINT32 kAnalysisRate = 48'000;           // Downstream analyzer assumption.
constexpr int kRestartDelayMs = 400;               // Backoff after device loss.

// The interface GUIDs come from __uuidof rather than hand-written literals: a
// mistyped GUID byte compiles fine and then fails at CoCreateInstance with an
// error that says nothing about which GUID was wrong.
constexpr CLSID kClsIdMMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
constexpr IID kIIdIMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
constexpr IID kIIdIAudioClient = __uuidof(IAudioClient);
constexpr IID kIIdIAudioCaptureClient = __uuidof(IAudioCaptureClient);

bool IsFloatMixFormat(const WAVEFORMATEX* format) {
    // WASAPI shared-mode mix format is float32, but a machine can surprise us, and
    // reading int16 as float would produce noise that looks exactly like a real signal.
    if (!format) return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return format->wBitsPerSample == 32;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return format->wBitsPerSample == 32 &&
               extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    return false;
}

} // namespace

struct MiaoWallpaperAudioTap::Impl {
    // Notification sink so a device switch does not leave the tap reading a dead
    // endpoint. Only sets a flag; all COM work stays on the capture thread.
    struct DeviceEvents final : IMMNotificationClient {
        DeviceEvents() = default;
        DeviceEvents(const DeviceEvents&) = delete;
        DeviceEvents& operator=(const DeviceEvents&) = delete;

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
            if (!object) return E_POINTER;
            if (riid == __uuidof(IMMNotificationClient) || riid == __uuidof(IUnknown)) {
                *object = static_cast<IMMNotificationClient*>(this);
                AddRef();
                return S_OK;
            }
            *object = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(refs.fetch_add(1) + 1); }
        ULONG STDMETHODCALLTYPE Release() override {
            const ULONG remaining = refs.fetch_sub(1) - 1;
            if (remaining == 0) delete this;
            return remaining;
        }
        HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
            deviceChanged.store(true);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
            return S_OK;
        }

        std::atomic<std::uint32_t> refs{1};
        std::atomic<bool> deviceChanged{false};
    };

    ~Impl() { Stop(); }

    bool Start() {
        std::lock_guard<std::mutex> guard(lifecycle_);
        if (worker.joinable()) return true;  // Already running.
        stopRequested.store(false);
        worker = std::thread([this] { Run(); });
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> guard(lifecycle_);
            if (!worker.joinable()) return;
            stopRequested.store(true);
        }
        wake.notify_all();
        worker.join();
    }

    bool Running() const noexcept { return running.load(); }

    bool LatestFrame(content::inputbus::AudioSpectrumFrame* out) const {
        if (!out) return false;
        std::lock_guard<std::mutex> guard(frame_);
        if (!hasFrame) return false;
        *out = latest;
        return true;
    }

    std::wstring LastErrorText() const {
        std::lock_guard<std::mutex> guard(error_);
        return lastError;
    }

    void SetError(std::wstring message) {
        std::lock_guard<std::mutex> guard(error_);
        lastError = std::move(message);
    }

private:
    // Runs on the capture thread. Owns every COM object it creates so that the
    // apartment's lifetime is exactly this function's.
    void Run() {
        HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool ownsApartment = SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE;
        DeviceEvents events;
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (SUCCEEDED(CoCreateInstance(kClsIdMMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                      kIIdIMMDeviceEnumerator, &enumerator)) &&
            enumerator) {
            enumerator->RegisterEndpointNotificationCallback(&events);
        }

        running.store(true);
        while (!stopRequested.load()) {
            CaptureOnce(enumerator.Get(), events);
            if (stopRequested.load()) break;
            // Device loss, or an initialisation failure: back off and retry rather
            // than reporting a dead wallpaper.
            std::unique_lock<std::mutex> lock(lifecycle_);
            wake.wait_for(lock, std::chrono::milliseconds(kRestartDelayMs),
                          [this] { return stopRequested.load(); });
        }

        if (enumerator) enumerator->UnregisterEndpointNotificationCallback(&events);
        running.store(false);
        if (ownsApartment) CoUninitialize();
    }

    void CaptureOnce(IMMDeviceEnumerator* enumerator, DeviceEvents& events) {
        if (!enumerator) {
            SetError(L"WASAPI 设备枚举器不可用");
            return;
        }
        events.deviceChanged.store(false);

        ComPtr<IMMDevice> device;
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) || !device) {
            SetError(L"没有可用的默认播放设备");
            return;
        }
        ComPtr<IAudioClient> client;
        if (FAILED(device->Activate(kIIdIAudioClient, CLSCTX_ALL, nullptr, &client)) || !client) {
            SetError(L"播放设备不提供音频客户端");
            return;
        }

        WAVEFORMATEX* mixFormat = nullptr;
        if (FAILED(client->GetMixFormat(&mixFormat)) || !mixFormat) {
            SetError(L"读不到播放设备混音格式");
            return;
        }
        struct FormatRelease {
            WAVEFORMATEX* format;
            ~FormatRelease() { if (format) CoTaskMemFree(format); }
        } formatScope{mixFormat};

        if (!IsFloatMixFormat(mixFormat)) {
            SetError(L"播放设备混音格式不是 32 位浮点,暂时无法做音频分析");
            return;
        }

        // An event-driven stream: sleeping for a fixed period would either burn a core
        // or miss packets, and the event fires once per device period.
        HANDLE packetReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!packetReady) {
            SetError(L"无法创建音频包事件");
            return;
        }
        struct EventRelease {
            HANDLE handle;
            ~EventRelease() { if (handle) CloseHandle(handle); }
        } eventScope{packetReady};

        HRESULT initialised = client->Initialize(
            AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            kBufferHns, 0, mixFormat, nullptr);
        if (FAILED(initialised)) {
            // Ducking/system effects can reject the event-driven attempt; retry once
            // without the flag. IAudioClient has no getter for the flags it was given,
            // so the chosen path is recorded here rather than queried back.
            initialised = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                             AUDCLNT_STREAMFLAGS_LOOPBACK,
                                             kBufferHns, 0, mixFormat, nullptr);
            eventDriven = false;
        } else {
            eventDriven = true;
        }
        if (FAILED(initialised)) {
            SetError(L"WASAPI loopback 初始化失败 HRESULT=" + HexValue(initialised));
            return;
        }
        if (eventDriven && FAILED(client->SetEventHandle(packetReady))) {
            SetError(L"无法把事件绑定到音频流");
            return;
        }

        ComPtr<IAudioCaptureClient> capture;
        if (FAILED(client->GetService(kIIdIAudioCaptureClient, &capture)) || !capture) {
            SetError(L"播放设备不提供采集客户端");
            return;
        }

        SetError(std::wstring());
        if (FAILED(client->Start())) {
            SetError(L"WASAPI loopback 启动失败");
            return;
        }

        // The loopback device period is what paces the thread; ask for it rather than
        // guessing, because a period of 10 ms vs 1.3 ms changes the sleep by 8x.
        REFERENCE_TIME periodHns = 0;
        client->GetDevicePeriod(&periodHns, nullptr);
        const DWORD waitMs = static_cast<DWORD>(std::max<REFERENCE_TIME>(1, periodHns / 10'000));

        while (!stopRequested.load() && !events.deviceChanged.load()) {
            if (WaitForSingleObject(packetReady, waitMs) != WAIT_OBJECT_0 && !eventDriven) {
                // No event handle (retry path): fall back to polling.
                std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
            }
            DrainPackets(capture.Get(), mixFormat);
            if (events.deviceChanged.load() || stopRequested.load()) break;
        }
        client->Stop();
    }

    void DrainPackets(IAudioCaptureClient* capture, const WAVEFORMATEX* format) {
        const std::size_t channels = format->nChannels > 0 ? static_cast<std::size_t>(format->nChannels) : 1;
        const double rate = format->nSamplesPerSec > 0 ? static_cast<double>(format->nSamplesPerSec) : 48000.0;
        UINT32 packet = 0;
        while (packet == 0 && SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            if (data && frames > 0) {
                const auto* interleaved = reinterpret_cast<const float*>(data);
                content::inputbus::AudioSpectrumFrame frame;
                if (content::inputbus::FeedAnalyzer(interleaved, frames, channels, rate,
                                                    static_cast<double>(kAnalysisRate),
                                                    &analyzer, &frame) > 0) {
                    std::lock_guard<std::mutex> guard(frame_);
                    latest = frame;
                    hasFrame = true;
                }
            }
            capture->ReleaseBuffer(frames);
            packet = 0;
        }
    }

    static std::wstring HexValue(unsigned long long value) {
        wchar_t buffer[32]{};
        swprintf_s(buffer, L"0x%llX", value);
        return std::wstring(buffer);
    }

    std::thread worker;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> running{false};
    // True when Initialize succeeded with AUDCLNT_STREAMFLAGS_EVENTCALLBACK and the
    // wait can block on the event; false on the polling fallback path.
    bool eventDriven{false};

    // Suffixed on purpose: DrainPackets has a local `frame` (the analysis result) and
    // a plain `frame` mutex name would silently shadow it, binding the lock_guard to
    // the wrong object.
    std::condition_variable wake;
    mutable std::mutex lifecycle_;     // Guards worker + wake.
    mutable std::mutex frame_;         // Guards the published analysis frame.
    mutable std::mutex error_;         // Guards lastError.

    content::inputbus::AudioSpectrumAnalyzer analyzer;
    content::inputbus::AudioSpectrumFrame latest;
    bool hasFrame{false};
    std::wstring lastError;
};

MiaoWallpaperAudioTap::MiaoWallpaperAudioTap() : impl_(std::make_unique<Impl>()) {}
MiaoWallpaperAudioTap::~MiaoWallpaperAudioTap() = default;

bool MiaoWallpaperAudioTap::Start() { return impl_ && impl_->Start(); }
void MiaoWallpaperAudioTap::Stop() { if (impl_) impl_->Stop(); }
bool MiaoWallpaperAudioTap::Running() const noexcept { return impl_ && impl_->Running(); }
bool MiaoWallpaperAudioTap::LatestFrame(content::inputbus::AudioSpectrumFrame* out) const {
    return impl_ && impl_->LatestFrame(out);
}
std::wstring MiaoWallpaperAudioTap::LastErrorText() const {
    return impl_ ? impl_->LastErrorText() : std::wstring();
}

} // namespace miaodesk::wallpaper
