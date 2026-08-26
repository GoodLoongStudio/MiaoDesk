# TuringDesk AI Generated Desktop Sandbox

Status: implementation contract for AI-generated desktop widgets and wallpapers.

## 1. Overall architecture

```text
User natural language
        |
        v
+---------------------------+
| Pi Agent                  |
| - understands intent      |
| - NEVER mutates desktop   |
| - outputs declarative     |
|   A2UI / asset descriptor |
+-------------+-------------+
              |
              v
+---------------------------+
| A2UIParser / Policy Gate  |
| - strict JSON parse       |
| - schema allowlist        |
| - size/range limits       |
| - rejects unknown fields  |
| - rejects code/script     |
+-------------+-------------+
              |
              v
+---------------------------+
| GeneratedContentSandbox   |
| %TEMP%/TuringDesk/        |
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
| AI JSON is data only      |
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
               +---------+---------+
               |                   |
               v                   v
        WallpaperService      WidgetService
        / Desktop engine      / desktop store
```

Hard rule: preview creation is not an apply operation. The sandbox must not call `ApplyWebPackage`, `CreateWebWidget`, modify the registry, write system folders, or alter the persisted desktop state. Only the explicit Apply command may cross the commit boundary.

TuringDesk currently has a native Win32 shell. The target host architecture is WinUI 3 / C++/WinRT. The sandbox contract is deliberately host-agnostic: the current native shell can host the same isolated WebView2 preview while the WinUI 3 shell is introduced without changing the A2UI protocol.

## 2. Declarative A2UI JSON protocol

AI output for widgets is JSON only. AI-generated JavaScript, HTML, CSS, C++, PowerShell, command lines and inline event handlers are forbidden.

Canonical schema lives at `docs/schemas/a2ui-widget.schema.json`.

Required node fields:

- `type`: one of `Card`, `Text`, `Button`, `Weather`, `List`.
- `props`: allowlisted data/style properties for that exact component type.
- `layout`: normalized placement and alignment.

Unknown fields are rejected (`additionalProperties: false`). Component count, text length, list length, numeric ranges and nesting depth are bounded by the native parser even if a future model produces technically valid but abusive JSON.

Example:

```json
{
  "version": 1,
  "type": "Card",
  "props": {
    "title": "今日待办",
    "background": "#CCFFFFFF",
    "foreground": "#1F2937",
    "cornerRadius": 18,
    "children": [
      {
        "type": "List",
        "props": {
          "items": ["整理需求", "完成预览", "提交版本"],
          "foreground": "#1F2937"
        },
        "layout": { "x": 0.05, "y": 0.22, "width": 0.90, "height": 0.68 }
      }
    ]
  },
  "layout": { "x": 0.68, "y": 0.05, "width": 0.28, "height": 0.18 }
}
```

Apply and Reject buttons are host-owned chrome. The AI is not allowed to manufacture an Apply action inside its JSON.

## 3. Key C++ / WinUI 3 / WebView2 implementation

### Transparent WebView2

The preview WebView2 is created by CoreHost/SandboxRenderer, never by AI output. With C++/WinRT the controller is created for a host HWND and the controller background is made transparent:

```cpp
wil::com_ptr<ICoreWebView2Controller> controller;
wil::com_ptr<ICoreWebView2> webview;

CreateCoreWebView2EnvironmentWithOptions(
    nullptr, userDataFolder.c_str(), nullptr,
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hostHwnd, &controller, &webview](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            RETURN_IF_FAILED(hr);
            return env->CreateCoreWebView2Controller(
                hostHwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [&controller, &webview](HRESULT hr2, ICoreWebView2Controller* value) -> HRESULT {
                        RETURN_IF_FAILED(hr2);
                        controller = value;
                        RETURN_IF_FAILED(controller->get_CoreWebView2(&webview));

                        wil::com_ptr<ICoreWebView2Controller2> controller2;
                        RETURN_IF_FAILED(controller.query_to(&controller2));
                        COREWEBVIEW2_COLOR transparent{0x00, 0xFF, 0xFF, 0xFF};
                        RETURN_IF_FAILED(controller2->put_DefaultBackgroundColor(transparent));
                        return S_OK;
                    }).Get());
        }).Get());
```

Equivalent background value is `0x00FFFFFF`: alpha 0, RGB white.

### Native -> WebView2 data bridge

The native parser first validates AI JSON and then sends a normalized JSON document to a fixed renderer that ships with TuringDesk:

```cpp
std::string normalized;
A2UIValidationError error;
if (!A2UIParser::ValidateAndNormalize(modelJson, normalized, error)) {
    ShowPreviewError(error.message);
    return;
}

webview->PostWebMessageAsJson(Utf8ToWide(normalized).c_str());
```

The trusted renderer contains fixed application-owned JavaScript. It receives only data:

```javascript
chrome.webview.addEventListener('message', e => {
  renderA2UI(e.data); // renderA2UI is shipped by TuringDesk, never model-generated
});
```

Apply/Reject are preferably native WinUI 3 buttons outside the WebView2 content. If a WebView2-hosted preview toolbar is used, it may post only a tiny allowlisted message:

```javascript
chrome.webview.postMessage({ type: 'previewAction', action: 'apply', previewId });
```

C++ accepts only `previewAction/apply` or `previewAction/reject` and verifies the preview id against the active in-memory session before acting.

## 4. Dynamic wallpaper handling

Temporary assets live only under:

```text
%TEMP%/TuringDesk/AI_Generated/<previewId>/
```

Preview lifecycle:

1. Download or copy the generated image/video into the preview directory using a random preview id.
2. Enforce file size, extension/MIME, HTTPS-only remote source, decode timeout and path-canonicalization limits.
3. Render image previews with Direct2D/DirectX/WIC.
4. Render video previews with the existing TuringDesk media path (Media Foundation today; libVLC can be an optional backend if later required).
5. Do not change the current desktop while previewing.
6. On Apply, copy the validated asset into a managed TuringDesk library/package directory, then call the existing `WallpaperService`/`DesktopControlService` commit path.
7. On Reject/close, stop playback, release GPU/decoder resources and recursively delete the preview directory.

For normal Windows static wallpaper, `SystemParametersInfo(SPI_SETDESKWALLPAPER, ...)` is the relevant shell API. `DwmSetWindowAttribute` does not set the Windows wallpaper; it controls window/DWM attributes. TuringDesk should prefer its own WallpaperService because dynamic wallpapers, multi-monitor assignment and package lifecycle are product state, not a raw Windows wallpaper mutation.

Raw AI-generated GLSL conflicts with the hard rule that AI must not output executable code. Production-safe policy is therefore `shaderPreset + numeric uniforms`, where shader source is application-owned. If raw GLSL is ever enabled experimentally, it must be a separately gated, out-of-process GPU sandbox and must never be part of the default A2UI path.

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

- JSON parse/schema failure: reject before WebView2 navigation; show a safe error card.
- WebView2 process crash: handle `ProcessFailed`, mark preview failed, release controller/environment references, remove temp content. The persisted desktop is untouched.
- Navigation is locked to `about:blank`, local virtual-host mapping, or an application-owned renderer. Block arbitrary navigation/new-window requests.
- Disable DevTools/context menus/status bar for normal users.
- No model-generated scripts, event attributes, external script tags or shell URIs.
- Remote media: HTTPS only, bounded download size, timeout, content-type validation and no automatic credential forwarding.
- Apply performs a second validation from disk immediately before commit (TOCTOU defense).
- Commit uses managed package directories and atomic replace/rename where possible.
- If commit fails, keep previous WallpaperService/WidgetService state and delete the candidate package.
- All COM callbacks use RAII smart pointers; destruction cancels timers/downloads and releases WebView2/decoder/GPU resources.
- No crash path is allowed to write registry/system directories or tear down the Windows shell.

## 6. VS2022 / source layout

Target solution layout:

```text
TuringDesk.sln
|
+-- CoreHost/
|   +-- App.xaml / MainWindow.xaml        # WinUI 3 host
|   +-- PreviewCoordinator.*              # user intent + Apply/Reject boundary
|   +-- WallpaperCommitAdapter.*
|   +-- WidgetCommitAdapter.*
|
+-- SandboxRenderer/
|   +-- SandboxPreviewWindow.xaml
|   +-- WebView2SandboxHost.*
|   +-- TrustedA2UIRenderer.html/js        # application-owned code only
|   +-- MediaPreviewHost.*
|
+-- A2UIParser/
|   +-- A2UIParser.*
|   +-- A2UISchema.*
|   +-- A2UINormalizer.*
|   +-- A2UIPolicy.*
|
+-- ExistingNativeCore/
    +-- WallpaperService
    +-- WidgetService
    +-- DesktopControlService
```

Dependency direction:

```text
CoreHost -> SandboxRenderer -> A2UIParser
CoreHost -> ExistingNativeCore
SandboxRenderer -X-> ExistingNativeCore state mutation
A2UIParser      -X-> UI / desktop services
Pi Agent        -X-> ExistingNativeCore mutation APIs
```

`SandboxRenderer` can request an Apply/Reject decision, but only `CoreHost` owns the commit capability.

## Built-in showcase content

Examples are product-owned, deterministic and always available even with no AI provider configured.

Wallpapers:

1. Aurora Flow — soft blue/violet procedural-style visual using an application-owned renderer preset.
2. Neon Flow — darker neon gradient/motion preset.
3. Ocean Glass — blue ocean/light-caustics preset suitable for demonstrating dynamic wallpaper preview.

Widgets:

1. Today Tasks — transparent todo card.
2. Focus Clock — clock/focus card.
3. Weather Glass — compact weather card using host-provided data slots.
4. System Pulse — CPU/memory/status card using host-provided data slots.

Examples must use the exact same sandbox, validation and Apply/Reject path as AI-generated content. No privileged example-only shortcut is allowed.
