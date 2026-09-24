// Renders a textured sprite and an analytic comet through the D3D11 backend and
// reads both final frames back.
//
// Why this file exists: the D3D11 textured draw path (SpriteDrawPath::SpriteTexture +
// MiaoBuiltinTextured + t0) has been compiled and linked for a while, and until now
// that was all the evidence it ever had. "It compiles" is not "it draws" — an HLSL
// shader is compiled at runtime by D3DCompile, so a broken shader, a missing t0 bind or
// a wrong sampler would all survive every compile-and-link gate in this repository.
//
// So this draws one frame into a real swap chain, copies the scene colour target out of
// the GPU, and asserts on two specific pixels. The shim clears to opaque black and the
// texture is flat magenta; the sprite is deliberately half-scale so both are visible:
//
//   · centre is magenta -> the texture was sampled and written (the path ran)
//   · corner is black   -> the clear survived, so the magenta is a sprite and not the
//                          whole target having been filled with the texture's colour
//
// The second assertion is the one a naive `any pixel is magenta` check would miss. It
// matters more than it looks: the D2D path's own textured test deliberately uses
// scale 1.0 and asserts full coverage, which is the right thing for *that* test and the
// wrong thing here — full coverage cannot distinguish a drawn sprite from a fill.
//
// The fixture here is deliberately separate from the one verify-scene-fixture-parity.sh
// pins. That one exists to prove a *scene document* is well formed on every machine and
// is rendered by the D2D backend; this one exists to put one textured sprite in front of
// a D3D11 device. It is minimal on purpose — one node, one transform, one sprite — so
// that a failure means the textured path and nothing else.
#include "miaodesk/MiaoAnalyticParticleField.h"
#include "miaodesk/MiaoSceneD3D11Renderer.h"

#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
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

// Printed *before* each risky call rather than after. The first run died with
// 0xC0000005 and, with default buffering, took everything it had printed with it — so
// there was nothing to read and nothing to conclude. With unbuffered stdout and a line
// emitted before each phase, the last line before the silence names the phase that
// died, which is the only way to diagnose an access violation I cannot reproduce
// locally.
void Phase(const char* what) { std::printf("  -- %s\n", what); }

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
    // This block is deliberately identical to MiaoSceneD2DRenderer's WriteSelfTestPng,
    // which is proven on this exact runner. My first version drifted from it in two ways —
    // it passed nullptr for the frame's property bag and it called
    // SetPixelFormat(PBGRA), which PNG does not support. Neither was the crash, but
    // "identical to the path that works" is worth more here than a shorter version.
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
    if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf()))) return false;
    if (FAILED(frame->Initialize(nullptr))) return false;
    if (FAILED(frame->SetSize(2, 2))) return false;
    if (FAILED(frame->WriteSource(bitmap.Get(), nullptr))) return false;
    if (FAILED(frame->Commit())) return false;
    return SUCCEEDED(encoder->Commit());
}

} // namespace

int wmain() {
    // Unbuffered, deliberately. The first run of this test died with 0xC0000005, and with
    // default buffering everything it had printed went down with it — the log would have
    // been empty and I would have learned nothing about where. One line per Step, flushed
    // as it happens, is what makes an access violation locatable.
    setvbuf(stdout, nullptr, _IONBF, 0);

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
    //
    // scale is 0.5, not 1.0, and that is the whole point. At scale 1.0 with
    // cornerRadius 0 the sprite covers the entire surface — that is what the D2D path
    // asserts (PixelCoverage > 0.5, "整块被 brush 覆盖"). Full coverage is a *weaker*
    // assertion for this test: a renderer that filled the whole target with the texture's
    // colour would satisfy it just as well as one that drew a sprite. At 0.5 the centre
    // is the sprite and the corners are the clear colour, so "the texture was sampled"
    // and "the magenta is the sprite, not a blanket fill" can be checked separately.
    constexpr std::string_view sceneJson = R"json({
      "schema":1,"id":"scene://selftest-d3d11-textured","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root",
      "nodes":[
        {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
        {"id":"node://panel","name":"Panel","parentId":"node://root","enabled":true,"components":[
          {"id":"component://panel/transform","kind":"transform","properties":[
            {"name":"position","type":"vec2","default":[0.0,0.0]},
            {"name":"scale","type":"vec2","default":[0.5,0.5]},
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

    Phase("创建 WIC 工厂并写包文件");
    ComPtr<IWICImagingFactory> wic;
    const bool wrote = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                  IID_PPV_ARGS(wic.GetAddressOf()))) &&
        WriteTextFile(root / L"manifest.json", manifestJson) &&
        WriteTextFile(root / L"parameters.json", parametersJson) &&
        WriteTextFile(root / L"scene.json", sceneJson) &&
        WriteMagentaPng(wic.Get(), root / L"assets" / L"panel.png");
    Step(wrote, "写出含 2x2 品红 PNG 的贴图包");

    // A real window is required: the renderer builds a swap chain for the HWND it is
    // given.
    //
    // WS_VISIBLE is deliberate. A swap chain for a window that has never been shown is a
    // configuration nothing else in this repo exercises — the host creates its slots with
    // WS_CHILD | WS_VISIBLE before handing the HWND to this renderer — and Present on an
    // unshown window is exactly the kind of thing that works on one driver and not
    // another. Matching the host costs one 64x64 popup flashing on a CI desktop.
    Phase("创建测试窗口");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kTestWindowClass;
    RegisterClassExW(&wc);
    const HWND window = CreateWindowExW(0, kTestWindowClass, L"", WS_POPUP | WS_VISIBLE,
                                        0, 0, static_cast<int>(kSurfaceSize),
                                        static_cast<int>(kSurfaceSize),
                                        nullptr, nullptr, wc.hInstance, nullptr);
    Step(window != nullptr, "创建 64x64 测试窗口(交换链需要真实 HWND)");

    // Present goes through DXGI, which occasionally needs the window's queue serviced
    // before it will complete. The host pumps its own loop; this test does not, so pump
    // once here rather than leave a driver-dependent failure in place.
    auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    };
    pump();

    std::wstring error;
    Phase("renderer.Load");
    MiaoSceneD3D11Renderer renderer;
    const bool loaded = wrote && window &&
        renderer.Load(root, window, &error);
    std::printf("      Load -> %s\n", loaded ? "ok" : "失败");
    if (!loaded && !error.empty()) {
        std::printf("      %ls\n", error.c_str());
    }
    Step(loaded, "加载贴图场景(该场景一个 material 都没有)");

    Phase("renderer.Draw");
    const bool drew = loaded && renderer.Draw(1.0f, &error);
    pump();
    if (!drew && !error.empty()) std::printf("      Draw 失败: %ls\n", error.c_str());
    Step(drew, "绘制一帧");

    Phase("renderer.ReadBackPixels");
    std::vector<unsigned char> pixels;
    unsigned width = 0;
    unsigned height = 0;
    const bool read = drew && renderer.ReadBackPixels(&pixels, &width, &height, &error);
    if (!read && !error.empty()) std::printf("      %ls\n", error.c_str());
    Step(read && width > 0 && height > 0 && pixels.size() == static_cast<std::size_t>(width) * height * 4u,
         "把场景颜色目标读回 CPU(BGRA, tightly packed)");
    if (read) std::printf("      %ux%u, %zu 字节\n", width, height, pixels.size());

    // BGRA, tightly packed. Magenta is the written texture's colour; black is the shim's
    // clear colour. Two positions are checked rather than a histogram, because the two
    // questions are different: the centre asks "did the textured path run at all", the
    // corner asks "is what ran a sprite, or did something fill the whole target".
    // Bounds-checked, and the whole block below is gated on the readback having produced a
    // real buffer. Without that gate, a readback that returns true with zero dimensions
    // makes the centre/corner lookups index into an empty vector — which is an access
    // violation, and is the most likely cause of the first run's 0xC0000005.
    const bool framed = read && width > 0 && height > 0 &&
        pixels.size() == static_cast<std::size_t>(width) * height * 4u;
    auto pixelAt = [&](unsigned x, unsigned y) {
        if (x >= width || y >= height) return std::make_tuple(0u, 0u, 0u, 0u);
        const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4u;
        if (i + 3 >= pixels.size()) return std::make_tuple(0u, 0u, 0u, 0u);
        // Widened to unsigned on both branches: a tuple<unsigned char,...> and a
        // tuple<unsigned,...> in one lambda is a deduction conflict, and mingw's gate
        // caught exactly that on the first attempt.
        return std::make_tuple(static_cast<unsigned>(pixels[i + 0]),
                               static_cast<unsigned>(pixels[i + 1]),
                               static_cast<unsigned>(pixels[i + 2]),
                               static_cast<unsigned>(pixels[i + 3]));
    };
    auto isMagenta = [&](unsigned x, unsigned y) {
        const auto [b, g, r, a] = pixelAt(x, y);
        return a == 0xFF && r > 0x60 && b > 0x60 && g < 0x40;
    };
    auto isClearBlack = [&](unsigned x, unsigned y) {
        const auto [b, g, r, a] = pixelAt(x, y);
        return a == 0xFF && r == 0 && g == 0 && b == 0;
    };

    unsigned magenta = 0;
    unsigned black = 0;
    for (unsigned y = 0; framed && y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            if (isMagenta(x, y)) ++magenta;
            else if (isClearBlack(x, y)) ++black;
        }
    }
    std::printf("      %ux%u:品红 %u / 黑 %u(共 %u)\n", width, height, magenta, black,
                width * height);

    Step(framed && isMagenta(width / 2, height / 2),
         "中心像素是贴图的品红 —— 贴图被采样并写进了帧缓冲(这条路径真的跑了)");
    Step(framed && isClearBlack(1, 1),
         "左上角仍是清屏黑 —— 品红是 sprite,不是整块目标被填成贴图色");

    // Phase 2: the particle pass writes renderres://particle-color, so this also pins
    // ReadBackPixels to the render graph's *final* colour rather than scene-color.
    // A black solid sprite gives the D3D11 MVP backend the renderable it currently
    // requires while leaving every non-particle pixel unambiguously black.
    Phase("重载解析 comet 粒子场景");
    renderer.Reset();
    constexpr std::string_view particleSceneJson = R"json({
      "schema":1,"id":"scene://selftest-d3d11-analytic","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root",
      "nodes":[
        {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
        {"id":"node://background","name":"Background","parentId":"node://root","enabled":true,"components":[
          {"id":"component://background/transform","kind":"transform","properties":[
            {"name":"position","type":"vec2","default":[0.0,0.0]},
            {"name":"scale","type":"vec2","default":[1.0,1.0]},
            {"name":"rotation","type":"float","default":0.0},
            {"name":"opacity","type":"float","default":1.0}]},
          {"id":"component://background/sprite","kind":"spriteRenderer","properties":[
            {"name":"opacity","type":"float","default":1.0},
            {"name":"tint","type":"color","default":[1.0,1.0,1.0,1.0]},
            {"name":"cornerRadius","type":"float","default":0.0},
            {"name":"materialId","type":"string","default":"material://black"}]}
        ]}
      ],
      "assets":[],"shaders":[],
      "materials":[{"id":"material://black","model":"builtin","builtinName":"solidColor","properties":[
        {"name":"color","type":"color","default":[0.0,0.0,0.0,1.0]}
      ],"textures":[]}],
      "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
      "bindings":[],"animations":[],
      "particleEmitters":[{
        "id":"particle://selftest/comet","enabled":true,"mode":"cometTrail",
        "analyticCount":1,"analyticColor":[1.0,0.84,0.98,1.0],
        "analyticSpeed":0.10,"analyticOpacity":1.0
      }]
    })json";
    const bool wroteParticleScene = WriteTextFile(root / L"scene.json", particleSceneJson);
    Step(wroteParticleScene, "写出带 CometTrail emitter 的场景");

    error.clear();
    const bool particleLoaded = wroteParticleScene && renderer.Load(root, window, &error);
    if (!particleLoaded && !error.empty()) std::printf("      Load 失败: %ls\n", error.c_str());
    Step(particleLoaded, "加载带解析粒子的 D3D11 Scene Runtime 场景");

    Phase("绘制解析粒子帧");
    const bool particleDrew = particleLoaded && renderer.Draw(0.0f, &error);
    pump();
    if (!particleDrew && !error.empty()) std::printf("      Draw 失败: %ls\n", error.c_str());
    Step(particleDrew, "绘制包含 analytic particle pass 的一帧");

    pixels.clear();
    width = 0;
    height = 0;
    error.clear();
    const bool particleRead =
        particleDrew && renderer.ReadBackPixels(&pixels, &width, &height, &error);
    if (!particleRead && !error.empty()) std::printf("      ReadBack 失败: %ls\n", error.c_str());
    const bool particleFramed = particleRead && width > 0 && height > 0 &&
        pixels.size() == static_cast<std::size_t>(width) * height * 4u;
    Step(particleFramed, "把 particle-color 最终目标读回 CPU");

    miaodesk::content::ParticleEmitterDefinition expectedEmitter;
    expectedEmitter.id = L"particle://selftest/comet";
    expectedEmitter.mode = miaodesk::content::ParticleEmitterMode::CometTrail;
    expectedEmitter.analyticCount = 1;
    expectedEmitter.analyticColor = miaodesk::content::Color4{1.0, 0.84, 0.98, 1.0};
    expectedEmitter.analyticSpeed = 0.10;
    expectedEmitter.analyticOpacity = 1.0;
    const auto expectedSamples = miaodesk::content::EvaluateAnalyticParticleField(
        expectedEmitter, static_cast<double>(width), static_cast<double>(height), 0.0);
    Step(particleFramed && !expectedSamples.empty() && expectedSamples.front().cross,
         "共享解析场产生带 cross 标记的 comet head");

    if (particleFramed && !expectedSamples.empty()) {
        const auto& head = expectedSamples.front();
        const int cx = std::clamp(static_cast<int>(std::lround(head.x)), 0,
                                  static_cast<int>(width) - 1);
        const int cy = std::clamp(static_cast<int>(std::lround(head.y)), 0,
                                  static_cast<int>(height) - 1);

        auto greenAt = [&](int x, int y) -> unsigned {
            if (x < 0 || y < 0 || x >= static_cast<int>(width) ||
                y >= static_cast<int>(height))
                return 0;
            const auto [b, g, r, a] = pixelAt(static_cast<unsigned>(x), static_cast<unsigned>(y));
            (void)b; (void)r; (void)a;
            return g;
        };
        unsigned headGreen = 0;
        for (int y = std::max(0, cy - 2);
             y <= std::min(static_cast<int>(height) - 1, cy + 2); ++y)
            for (int x = std::max(0, cx - 2);
                 x <= std::min(static_cast<int>(width) - 1, cx + 2); ++x)
                headGreen = std::max(headGreen, greenAt(x, y));

        const double radius = std::max(head.radiusX, head.radiusY);
        const int probeY = std::clamp(
            static_cast<int>(std::lround(head.y - radius * 2.25)),
            0, static_cast<int>(height) - 1);
        unsigned crossGreen = 0;
        for (int y = std::max(0, probeY - 1);
             y <= std::min(static_cast<int>(height) - 1, probeY + 1); ++y)
            for (int x = std::max(0, cx - 1);
                 x <= std::min(static_cast<int>(width) - 1, cx + 1); ++x)
                crossGreen = std::max(crossGreen, greenAt(x, y));

        std::printf("      analytic comet centre=(%d,%d), headGreen=%u, crossGreen=%u\n",
                    cx, cy, headGreen, crossGreen);
        Step(headGreen > 32,
             "comet 椭圆真实写进 D3D11 最终颜色目标");
        Step(crossGreen > 8,
             "comet 十字在椭圆外仍有 GPU 像素");
        Step(isClearBlack(1, 1),
             "粒子之外仍是黑底 —— particle pass 是叠加而不是整屏填充");
    }

    // Phase 3: PetalFall is intentionally anisotropic. A scalar particle size
    // would turn it into a circle, which is exactly the bug this GPU-instance change
    // is meant to prevent. Use a 512px target so the legacy radii span enough physical
    // pixels for a stable readback assertion.
    Phase("把测试窗口放大到 1024x1024 并验证 PetalFall 椭圆");
    renderer.Reset();
    SetWindowPos(window, nullptr, 0, 0, 1024, 1024,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    pump();

    constexpr std::string_view petalSceneJson = R"json({
      "schema":1,"id":"scene://selftest-d3d11-petal","kind":"wallpaper","profile":"wallpaper","rootNodeId":"node://root",
      "nodes":[
        {"id":"node://root","name":"Root","parentId":"","enabled":true,"components":[]},
        {"id":"node://background","name":"Background","parentId":"node://root","enabled":true,"components":[
          {"id":"component://background/transform","kind":"transform","properties":[
            {"name":"position","type":"vec2","default":[0.0,0.0]},
            {"name":"scale","type":"vec2","default":[1.0,1.0]},
            {"name":"rotation","type":"float","default":0.0},
            {"name":"opacity","type":"float","default":1.0}]},
          {"id":"component://background/sprite","kind":"spriteRenderer","properties":[
            {"name":"opacity","type":"float","default":1.0},
            {"name":"tint","type":"color","default":[1.0,1.0,1.0,1.0]},
            {"name":"cornerRadius","type":"float","default":0.0},
            {"name":"materialId","type":"string","default":"material://black"}]}
        ]}
      ],
      "assets":[],"shaders":[],
      "materials":[{"id":"material://black","model":"builtin","builtinName":"solidColor","properties":[
        {"name":"color","type":"color","default":[0.0,0.0,0.0,1.0]}
      ],"textures":[]}],
      "inputs":[{"id":"input://frame/time","type":"float","default":0.0}],
      "bindings":[],"animations":[],
      "particleEmitters":[{
        "id":"particle://selftest/petal","enabled":true,"mode":"petalFall",
        "analyticCount":1,"analyticColor":[0.2,1.0,0.2,1.0],
        "analyticOpacity":1.0
      }]
    })json";
    const bool wrotePetalScene = WriteTextFile(root / L"scene.json", petalSceneJson);
    Step(wrotePetalScene, "写出带 PetalFall emitter 的场景");

    error.clear();
    const bool petalLoaded = wrotePetalScene && renderer.Load(root, window, &error);
    if (!petalLoaded && !error.empty()) std::printf("      Load 失败: %ls\n", error.c_str());
    Step(petalLoaded, "加载 PetalFall D3D11 场景");

    const bool petalDrew = petalLoaded && renderer.Draw(0.0f, &error);
    pump();
    if (!petalDrew && !error.empty()) std::printf("      Draw 失败: %ls\n", error.c_str());
    Step(petalDrew, "绘制 PetalFall 一帧");

    pixels.clear();
    width = 0;
    height = 0;
    error.clear();
    const bool petalRead =
        petalDrew && renderer.ReadBackPixels(&pixels, &width, &height, &error);
    if (!petalRead && !error.empty()) std::printf("      ReadBack 失败: %ls\n", error.c_str());
    const bool petalFramed = petalRead && width > 0 && height > 0 &&
        pixels.size() == static_cast<std::size_t>(width) * height * 4u;
    Step(petalFramed, "把 PetalFall 最终颜色目标读回 CPU");

    miaodesk::content::ParticleEmitterDefinition petalEmitter;
    petalEmitter.id = L"particle://selftest/petal";
    petalEmitter.mode = miaodesk::content::ParticleEmitterMode::PetalFall;
    petalEmitter.analyticCount = 1;
    petalEmitter.analyticColor = miaodesk::content::Color4{0.2, 1.0, 0.2, 1.0};
    petalEmitter.analyticOpacity = 1.0;
    const auto petalSamples = miaodesk::content::EvaluateAnalyticParticleField(
        petalEmitter, static_cast<double>(width), static_cast<double>(height), 0.0);
    Step(petalFramed && !petalSamples.empty(), "共享解析场产生 PetalFall sample");

    if (petalFramed && !petalSamples.empty()) {
        const auto& petal = petalSamples.front();
        const int cx = std::clamp(static_cast<int>(std::lround(petal.x)), 0,
                                  static_cast<int>(width) - 1);
        const int cy = std::clamp(static_cast<int>(std::lround(petal.y)), 0,
                                  static_cast<int>(height) - 1);

        int minX = static_cast<int>(width);
        int minY = static_cast<int>(height);
        int maxX = -1;
        int maxY = -1;
        const int scan = 16;
        for (int y = std::max(0, cy - scan);
             y <= std::min(static_cast<int>(height) - 1, cy + scan); ++y) {
            for (int x = std::max(0, cx - scan);
                 x <= std::min(static_cast<int>(width) - 1, cx + scan); ++x) {
                const auto [b, g, r, a] =
                    pixelAt(static_cast<unsigned>(x), static_cast<unsigned>(y));
                (void)b; (void)r;
                // Background is exactly black; the green petal has a very large
                // green-to-background margin, so a small threshold keeps antialiasing
                // edge pixels while excluding the clear.
                if (a > 0 && g > 5) {
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
        }
        const int litWidth = maxX >= minX ? maxX - minX + 1 : 0;
        const int litHeight = maxY >= minY ? maxY - minY + 1 : 0;
        std::printf(
            "      petal centre=(%d,%d), expected radii=(%.3f,%.3f), lit=%dx%d\n",
            cx, cy, petal.radiusX, petal.radiusY, litWidth, litHeight);
        Step(litWidth > 0 && litHeight > 0,
             "PetalFall 椭圆真实写进 D3D11 最终颜色目标");
        Step(litHeight > litWidth,
             "PetalFall GPU 像素保持 radiusY > radiusX 的椭圆比例");
        Step(isClearBlack(1, 1),
             "PetalFall 之外仍是黑底");
    }

    Phase("销毁渲染器(离开作用域)");
    Phase("释放 COM 对象(必须在 CoUninitialize 之前)");
    if (window) DestroyWindow(window);
    UnregisterClassW(kTestWindowClass, wc.hInstance);
    // Every COM object goes away *before* the apartment does. Not tidiness — releasing
    // COM objects on an uninitialized apartment is undefined behaviour, and it is what
    // killed the first run: `renderer` held a device, a swap chain and render targets, and
    // `wic` held the imaging factory, and both were destroyed at scope exit, i.e. after
    // CoUninitialize had already torn the apartment down. 0xC0000005 with nothing before
    // it in the log.
    //
    // MiaoSceneD2DRenderer's self-test spells the same discipline out as five explicit
    // Reset() calls before its CoUninitialize; this is that list, one file over.
    renderer.Reset();
    wic.Reset();
    fs::remove_all(root, ec);
    if (shouldUninitialize) CoUninitialize();

    std::printf("\n%s(%d 处失败)\n", failures == 0 ? "ALL CHECKS PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
