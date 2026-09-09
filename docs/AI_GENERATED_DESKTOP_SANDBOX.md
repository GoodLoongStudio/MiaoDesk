# MiaoDesk AI Generated Wallpaper Sandbox

Status: implementation contract for AI-generated desktop wallpapers.

Desktop widgets are **native-only presets** (GlassClock / TodayTasks / WeatherGlass). AI cannot preview, generate, or apply widgets; Pi inspects them read-only through `desktop_widget_list`. Everything in this document covers wallpapers only.

## 1. Overall architecture

```text
User natural language
        |
        v
+---------------------------+
| Pi Agent                  |
| - understands intent      |
| - NEVER mutates desktop   |
| - calls desktop_preview_  |
|   wallpaper (preset or    |
|   local image/video path) |
+-------------+-------------+
              |
              v
+---------------------------+
| Preview tool (native)     |
| - allowlisted modes only  |
| - preset/image/video      |
| - size/extension limits   |
| - local assets copied     |
|   into the sandbox dir    |
+-------------+-------------+
              |
              v
+---------------------------+
| GeneratedContentSandbox   |
| %TEMP%/MiaoDesk/          |
| AI_Generated/<previewId>  |
| - ephemeral files only    |
| - no registry writes      |
| - no product state writes |
+-------------+-------------+
              |
              v
+---------------------------+
| SandboxRenderer           |
| WebView2 isolated preview |
| transparent background    |
| fixed trusted renderer JS |
| AI data is data only      |
+-------------+-------------+
              |
       +------+------+
       |             |
   Reject/Close     Apply
       |             |
       v             v
 delete temp   +------------------+
 no state      | CoreHost commit  |
 changes       | boundary         |
               | - validate again |
               | - copy assets    |
               | - atomic commit  |
               +---------+--------+
                         |
                         v
                WallpaperService / DesktopControlService
```

Hard rule: preview creation is not an apply operation. The sandbox must not call `ApplyWebPackage`, modify the registry, write system folders, or alter the persisted desktop state. Only the explicit Apply command may cross the commit boundary.

MiaoDesk uses a native Win32 shell built with C++23. **WinUI 3 / C++/WinRT is not a target architecture** and was explicitly rejected by the product baseline performance principle; the always-on desktop render layer stays Native C++ / Win32.

## 2. Tool surface

- `desktop_preview_wallpaper` — creates a sandbox preview for `preset` (`aurora_flow` / `neon_flow` / `ocean_flow`), `image`, or `video`. Remote sources must be HTTPS; local files are copied into the preview directory with extension and size limits (25 MiB images, 250 MiB videos).
- `desktop_preview_examples` — lists the built-in wallpaper examples.
- `wallpaper_validate_package` — validates an existing `.mdwall` package without applying it.
- `wallpaper_state_get` — read-only desktop state.

AI never outputs HTML, JavaScript, CSS, shell commands, or executable code. Preset keys are application-owned; image/video sources are host-copied data, not model markup.

## 3. Key C++ / Win32 / WebView2 implementation

### Transparent WebView2

The preview WebView2 is created by the host process (`GeneratedDesktopPreview.cpp`), never by AI output. The controller background is made transparent:

```cpp
ComPtr<ICoreWebView2Controller> controller;
ComPtr<ICoreWebView2> webview;

CreateCoreWebView2EnvironmentWithOptions(
    nullptr, userData.c_str(), nullptr,
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [state](HRESULT hr, ICoreWebView2Environment* environment) -> HRESULT {
            if (FAILED(hr) || !environment) return S_OK;
            return environment->CreateCoreWebView2Controller(
                state->hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [state](HRESULT controllerHr, ICoreWebView2Controller* controller) -> HRESULT {
                        if (FAILED(controllerHr) || !controller) return S_OK;
                        controller->get_CoreWebView2(state->webview.GetAddressOf());
                        ComPtr<ICoreWebView2Controller2> controller2;
                        if (SUCCEEDED(state->controller.As(&controller2)) && controller2) {
                            COREWEBVIEW2_COLOR transparent{0x00, 0xFF, 0xFF, 0xFF};
                            controller2->put_DefaultBackgroundColor(transparent);
                        }
                        return S_OK;
                    }).Get());
        }).Get());
```

Equivalent background value is `0x00FFFFFF`: alpha 0, RGB white.

### Native -> WebView2 data bridge

The native host builds a fixed, application-owned renderer page (`TrustedWallpaperRendererHtml`) that embeds the validated preset/image/video payload as base64 data and loads it with `NavigateToString`. The trusted renderer contains fixed application-owned JavaScript; AI output never reaches the WebView2 as code:

```javascript
// Shipped by MiaoDesk inside TrustedWallpaperRendererHtml, never model-generated.
const raw = Uint8Array.from(atob('<base64 payload>'), c => c.charCodeAt(0));
const p = JSON.parse(new TextDecoder().decode(raw));
// renders preset / image / video
```

Apply/Reject are native Win32 buttons owned by the host, outside the WebView2 content.

## 4. Dynamic wallpaper handling

Temporary assets live only under:

```text
%TEMP%/MiaoDesk/AI_Generated/<previewId>/
```

Preview lifecycle:

1. Copy the generated image/video into the preview directory using a random preview id.
2. Enforce file size, extension/MIME, HTTPS-only remote source and path-canonicalization limits.
3. Render presets with the application-owned CSS/JS renderer; render local image/video through data URIs / file URLs inside the same trusted page.
4. Do not change the current desktop while previewing.
5. On Apply, build a managed `.mdwall` package (`WallpaperPackage::CreateWeb`), copy the validated asset into it, then call the `DesktopControlService::ApplyWebPackage` commit path. A failed commit deletes the candidate package.
6. On Reject/close, recursively delete the preview directory.

For normal Windows static wallpaper, `SystemParametersInfo(SPI_SETDESKWALLPAPER, ...)` is the relevant shell API. `DwmSetWindowAttribute` does not set the Windows wallpaper; it controls window/DWM attributes. MiaoDesk should prefer its own WallpaperService because dynamic wallpapers, multi-monitor assignment and package lifecycle are product state, not a raw Windows wallpaper mutation.

Raw AI-generated GLSL conflicts with the hard rule that AI must not output executable code. Production-safe policy is application-owned presets only; if raw GLSL is ever enabled experimentally, it must be a separately gated, out-of-process GPU sandbox and must never be part of the default path.

## 5. Safety and rollback

Preview sessions use an explicit state machine:

```text
Created -> Validated -> Rendering -> AwaitingUser
                              |         |       |
                              |       Apply   Reject/Close
                              |         |       |
                              v         v       v
                            Failed   Committed Destroyed
```

Safety requirements:

- Mode/extension/size validation failure: reject before WebView2 navigation; show a safe error.
- WebView2 process crash: handle `ProcessFailed`, mark preview failed, release controller/environment references, remove temp content. The persisted desktop is untouched.
- Navigation is locked to the application-owned renderer page. No arbitrary navigation or new-window requests.
- Disable DevTools/context menus/status bar for normal users.
- No model-generated scripts, event attributes, external script tags or shell URIs.
- Remote media: HTTPS only, bounded size, extension validation and no automatic credential forwarding.
- Commit uses managed package directories and atomic replace/rename where possible.
- If commit fails, keep previous wallpaper state and delete the candidate package.
- All COM callbacks use RAII smart pointers; destruction cancels timers/downloads and releases WebView2/decoder/GPU resources.
- No crash path is allowed to write registry/system directories or tear down the Windows shell.

## 6. Current source layout

```text
src/
+-- desktop/preview/GeneratedDesktopPreview.cpp
|   +-- sandbox dir + kind/mode/source files
|   +-- TrustedWallpaperRendererHtml        # application-owned renderer only
|   +-- preview window (WebView2 + Apply/Reject)
|   +-- ApplyWallpaper commit boundary
+-- desktop/wallpaper/library/WallpaperPackage.cpp
|   +-- CreateWeb (.mdwall package writer)
+-- desktop/control/DesktopControlService.cpp
|   +-- ApplyWebPackage / ApplyLibraryItem commit path
+-- ai/pi/PiNativeToolsExtension.cpp
    +-- desktop_preview_wallpaper / desktop_preview_examples tool schemas
```

Dependency direction:

```text
Pi Agent      -> native tool host (read + preview only)
Preview tool  -> sandbox dir + COPYDATA to the running MiaoDesk UI
Sandbox window-> TrustedWallpaperRendererHtml (data only)
Apply button  -> DesktopControlService commit (the only mutation path)
Pi Agent      -X-> desktop mutation APIs
```

## Built-in showcase content

Examples are product-owned, deterministic and always available even with no AI provider configured.

Wallpapers:

1. aurora_flow — soft blue/violet procedural-style motion preset.
2. neon_flow — darker neon gradient/motion preset.
3. ocean_flow — blue ocean/light-caustics preset suitable for demonstrating dynamic wallpaper preview.

Examples use the exact same sandbox, validation and Apply/Reject path as AI-generated content. No privileged example-only shortcut is allowed.
