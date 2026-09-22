// Renders a textured sprite through the D3D11 backend and reads the pixels back.
//
// Why this file exists: the D3D11 textured draw path (SpriteDrawPath::SpriteTexture +
// MiaoBuiltinTextured + t0) has been compiled and linked for a while, and until now
// that was all the evidence it ever had. "It compiles" is not "it draws" — an HLSL
// shader is compiled at runtime by D3DCompile, so a broken shader, a missing t0 bind or
// a wrong sampler would all survive every compile-and-link gate in this repository.
//
// So this draws one frame into a real swap chain, copies the scene colour target out of
// the GPU, and asserts on the bytes. The shim's clear colour is opaque black and the
// texture is flat magenta, which makes the assertion two-sided rather than a smoke test:
//   · magenta pixels exist  -> the texture was sampled and written (the path ran)
//   · black pixels exist    -> the magenta is the sprite, not a blanket clear of the
//                              whole target, and not the scene colour being wrong
// A pass that came from "everything got filled with the texture's colour" would fail the
// second half, which is the half a naive `any pixel is magenta` check would miss.
//
// The fixture here is deliberately separate from the one verify-scene-fixture-parity.sh
// pins. That one exists to prove a *scene document* is well formed on every machine and
// is rendered by the D2D backend; this one exists to put one textured sprite in front of
// a D3D11 device. It is minimal on purpose — one node, one transform, one sprite — so
// that a failure means the textured path and nothing else.
#include "miaodesk/MiaoSceneD3D11Renderer.h"

#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

using miaodesk::content::MiaoSceneD3D11Renderer;

namespace {

constexpr wchar_t kTestWindowClass[] = L"MiaoDesk.SceneD3D11.SelfTest";
constexpr unsigned kSurfaceSize = 64;

int failures = 0;
void Step(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

bool WriteTextFile(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// A 2x2 flat magenta PNG, written through WIC so the bytes are a real PNG rather than a
// hand-rolled one (the loader decodes with WIC anyway, so writing with WIC is the honest
// round trip). Magenta because the shim clears to opaque black: no other colour in this
// scene is anywhere near it.
bool WriteMagentaPng(IWICImagingFactory* factory, const fs::path& path) {
    if (!factory) return false;
    constexpr UINT8 kMagentaBgra[4] = {0xB0, 0x00, 0xB0, 0xFF};

    ComPtr<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmap(2, 2, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapCacheOnLoad, bitmap.GetAddressOf())))
        return false;
    WICRect rect{0, 0, 2, 2};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(bitmap->Lock(&rect, WICBitmapLockWrite, lock.GetAddressOf()))) return false;
    UINT stride = 0;
    UINT bytes = 0;
    BYTE* data = nullptr;
    if (FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&bytes, &data)) || !data)
        return false;
    for (UINT y = 0; y < 2; ++y) {
        for (UINT x = 0; x < 2; ++x) {
            BYTE* pixel = data + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
            pixel[0] = kMagentaBgra[0];
            pixel[1] = kMagentaBgra[1];
            pixel[2] = kMagentaBgra[2];
            pixel[3] = kMagentaBgra[3];
        }
    }
    lock.Reset();

    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())))
        return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IWICStream> writer;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr))) return false;
    if (FAILED(frame->Initialize(nullptr))) return false;
    if (FAILED(frame->SetSize(2, 2))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(frame->SetPixelFormat(&format))) return false;
    if (FAILED(frame->WriteSource(bitmap.Get(), nullptr))) return false;
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

} // namespace

int wmain() {
    std::printf("D3D11 贴图 sprite 的真实渲染与回读\n");

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        std::printf("  [FAIL] CoInitializeEx\n");
        return 1;
    }

    std::error_code ec;
    const fs::path root = fs::temp_directory_path() /
        (L"MiaoDesk-SceneD3D11-Textured-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()) + L".mdwall");
    fs::remove_all(root, ec);
    fs::create_directories(root / L"assets", ec);

    // A minimal one-sprite scene. `texture` is the sprite's own assetReference and there
    // is deliberately no material in the package at all — that is the case this path is
    // for (a textured sprite needs no material; see MiaoSpriteMaterialPolicy).
    constexpr std::string_view sceneJson = R"json({
      "schema":1,"id":"scene://selftest-d3d11-textured","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root",
      "nodes":[
        {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
        {"id":"node://panel","name":"Panel","parentId":"node://root","enabled":true,"components":[
          {"id":"component://panel/transform","kind":"transform","properties":[
            {"name":"position","type":"vec2","default":[0.0,0.0]},
            {"name":"scale","type":"vec2","default":[1.0,1.0]},
            {"name":"rotation","type":"float","default":0.0},
            {"name":"opacity","type":"float","default":1.0}]},
          {"id":"component://panel/sprite","kind":"spriteRenderer","properties":[
            {"name":"opacity","type":"float","default":1.0},
            {"name":"tint","type":"color","default":[1.0,1.0,1.0,1.0]},
            {"name":"cornerRadius","type":"float","default":0.0},
            {"name":"texture","type":"assetReference","default":"asset://panel/texture"}]}
        ]}
      ],
      "assets":[{"id":"asset://panel/texture","type":"image","source":"assets/panel.png"}],
      "shaders":[],"materials":[],
      "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
      "bindings":[],"animations":[]
    })json";
    constexpr std::string_view manifestJson = R"json({
      "schema":1,"id":"com.goodloong.selftest-d3d11-textured","name":"Self Test D3D11 Textured",
      "author":"MiaoDesk","version":"1.0.0",
      "kind":"wallpaper","runtime":"scene","entry":"scene.json","parameters":"parameters.json","capabilities":[]
    })json";
    constexpr std::string_view parametersJson = R"json({"schema":1,"parameters":[]})json";

    ComPtr<IWICImagingFactory> wic;
    const bool wrote = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                  IID_PPV_ARGS(wic.GetAddressOf()))) &&
        WriteTextFile(root / L"manifest.json", manifestJson) &&
        WriteTextFile(root / L"parameters.json", parametersJson) &&
        WriteTextFile(root / L"scene.json", sceneJson) &&
        WriteMagentaPng(wic.Get(), root / L"assets" / L"panel.png");
    Step(wrote, "写出含 2x2 品红 PNG 的贴图包");

    // A real window is required: the renderer builds a swap chain for the HWND it is
    // given. It never has to be visible for one frame to be drawn and read back.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kTestWindowClass;
    RegisterClassExW(&wc);
    const HWND window = CreateWindowExW(0, kTestWindowClass, L"", WS_POPUP,
                                        0, 0, static_cast<int>(kSurfaceSize),
                                        static_cast<int>(kSurfaceSize),
                                        nullptr, nullptr, wc.hInstance, nullptr);
    Step(window != nullptr, "创建 64x64 测试窗口(交换链需要真实 HWND)");

    std::wstring error;
    MiaoSceneD3D11Renderer renderer;
    const bool loaded = wrote && window &&
        renderer.Load(root, window, &error);
    std::printf("      Load -> %s\n", loaded ? "ok" : "失败");
    if (!loaded && !error.empty()) {
        std::printf("      %ls\n", error.c_str());
    }
    Step(loaded, "加载贴图场景(该场景一个 material 都没有)");

    const bool drew = loaded && renderer.Draw(1.0f, &error);
    if (!drew && !error.empty()) std::printf("      %ls\n", error.c_str());
    Step(drew, "绘制一帧");

    std::vector<unsigned char> pixels;
    unsigned width = 0;
    unsigned height = 0;
    const bool read = drew && renderer.ReadBackPixels(&pixels, &width, &height, &error);
    if (!read && !error.empty()) std::printf("      %ls\n", error.c_str());
    Step(read && width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4u,
         "把场景颜色目标读回 CPU(BGRA, tightly packed)");
    if (read) std::printf("      %ux%u, %zu 字节\n", width, height, pixels.size());

    // BGRA, tightly packed. Magenta is the written texture's colour; black is the shim's
    // clear. Both halves are asserted, for the reason in the file header.
    unsigned magenta = 0;
    unsigned black = 0;
    unsigned other = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const unsigned b = pixels[i + 0];
        const unsigned g = pixels[i + 1];
        const unsigned r = pixels[i + 2];
        const unsigned a = pixels[i + 3];
        if (a == 0xFF && r > 0x60 && b > 0x60 && g < 0x40) ++magenta;
        else if (a == 0xFF && r == 0 && g == 0 && b == 0) ++black;
        else ++other;
    }
    const unsigned total = magenta + black + other;
    std::printf("      品红 %u / 黑 %u / 其它 %u(共 %u)\n", magenta, black, other, total);

    Step(read && magenta > 0, "有像素是贴图的品红 —— 贴图被采样并写进了帧缓冲");
    Step(read && black > 0, "也有像素是清屏黑 —— 品红来自 sprite,不是整块被填成贴图色");

    if (window) DestroyWindow(window);
    UnregisterClassW(kTestWindowClass, wc.hInstance);
    fs::remove_all(root, ec);
    if (shouldUninitialize) CoUninitialize();

    std::printf("\n%s(%d 处失败)\n", failures == 0 ? "ALL CHECKS PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
