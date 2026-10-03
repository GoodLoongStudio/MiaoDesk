# MiaoDesk AI Generated Wallpaper Sandbox

Status: implementation contract for AI-generated desktop wallpapers, plus the
corrected boundary for AI-generated widgets.

**Corrected 2026-09-27.** An earlier revision of this file stated that "Desktop
widgets are native-only presets… AI cannot preview, generate, or apply widgets."
That was wrong for widgets *content packages* and is retracted here. The AI
Content Creator does produce candidate `.mdwidget` packages. What remains true is
narrower and still absolute: no surface accepts model-authored executable code.

Three surfaces are easy to confuse and must not be merged:

| Surface | What it does | Can the AI create a widget here? |
| --- | --- | --- |
| Pi tool surface (§2) | `desktop_preview_wallpaper` sandboxes wallpapers; `desktop_widget_list` lists installed widgets | No — read-only (`DesktopWidgetTools.cpp:107`) |
| AI Content Creator | user-initiated conversation that produces a previewable `.mdwidget` / `.mdwall` content package | Yes — as a structured package, never raw markup (`ContentCreatorBridge.cpp:52`) |
| Formal widget APIs | runtime mutation of installed widgets | No tool maps to it (`WidgetService::Update`) |

The invariant across all three: **AI output is data, not code.** No surface
accepts model-authored HTML/CSS/JavaScript. A `.mdwidget` is a structured
content package (`ContentKind::Widget`), not markup — so creating one is not the
"generate widget HTML" that the Pi safety prompt rightly forbids.

How the Content Creator is reached: the wallpaper library shows
`✨ AI 制作组件` on the widget page and `✨ AI 制作壁纸` on the wallpaper page;
`SearchWindow::OpenContentCreator` opens the dialog (`SearchWindow.cpp:386`).
Its prompt asks the user's requirements first and instructs a *previewable*
package, explicitly not to reach the desktop unconfirmed.

### What "the AI cannot change a widget" does and does not mean

Stated precisely, because the loose version is false:

- **No tool path exists.** `desktop_widget_list` and `wallpaper_state_get` are the
  only widget-facing tools, both read-only, and the Pi worker allowlist rejects
  product-state mutation with exit code 26.
- **But the allowlist is not airtight.** Pi is also granted the generic file tools
  `read,edit,write,bash,grep,find,ls`, and the widget store is a plain INI file
  with no checksum or signature on load. So the guarantee is "no *tool* mutates
  widget state", not "the model can never affect it".
- **The residual risk is bounded, not eliminated.** `DesktopWidgetStore::Normalize`
  clamps geometry into `[0.05, 1.0]` and forces `x + width <= 1.0`, so a tampered
  width cannot push a built-in widget off-screen; it can still change which widget
  is enabled or its title.

Closing this properly means either removing the generic file tools from the Pi
allowlist or signing the store. Until then, the boundary above is what actually
holds — recorded here rather than claimed as stronger.

### Professional roadmap boundary — 2026-10-03

The professional goals are defined in [PROFESSIONAL_DESKTOP_PLAN.md](PROFESSIONAL_DESKTOP_PLAN.md). They do not automatically widen the current Creator tool or artifact surface. CAP-01 must distinguish runtime support, preview support and AI authoring support. ADV-04 is a future, separately gated programmable-visual authoring change; before enabling it, update this contract, product enforcement, Skills, compiler/resource limits and isolation evidence together. Existing historical risk observations above must be rechecked by PRO-01 rather than assumed to describe the latest implementation.

Everything else in this document describes the **wallpaper** sandbox.

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

### Library install is not the commit boundary (corrected 2026-09-27)

An earlier reading of the rule above treated "install" and "apply" as one step, and the
AI Content Creator therefore required an explicit 加入壁纸库 / 加入组件库 click before a
validated package reached the library. That was wrong twice over:

- it protected nothing the rule was protecting — the library is application-managed
  storage, not desktop state, and no render or selection changes when a package enters it;
- it cost the user the artifact. A user who closed the creator after a successful
  generation but before clicking 加入壁纸库 lost the package entirely: it lived only in the
  model's sandbox directory.

So the boundary is now stated as exactly what the rule says:

| Step | Who does it | Crosses the commit boundary? |
| --- | --- | --- |
| generate + validate | the host, when a package is produced | no |
| install into the library | **automatic** on validation success (`ContentCreatorDialog.cpp`, `InstallToLibrary`) | no |
| apply to the desktop | only the explicit `应用到桌面` / `添加到桌面` button | **yes** |

`InstallToLibrary` is deliberately the same function the explicit button calls, so the
automatic path cannot become a second, weaker install.

### A generated Scene wallpaper now applies (corrected 2026-09-27, same day)

Applying one used to fail outright. The global entry persists a builtin scene key, or an
image / video / web source, so a Content Scene package had nothing to be written into it
and `WallpaperService::ApplyLibraryItem` returned "该配置化 Scene 已进入内容框架；当前全局/跨屏入口仍只接受内置
Scene，请在目标显示器上分配该壁纸"— on the target the library defaults to, for every
generated wallpaper and every imported `.mdwall` scene package.

The fix does not add a global capability. It routes to the mechanism that already works:

1. `DesktopControlService::ApplyLibraryItem` asks `WallpaperService::NeedsPerMonitorApply`,
   which is true only for a Scene whose id is `content:<id>` with no builtin runtime key.
   Builtin scenes, images, videos and Web wallpapers keep taking the global path unchanged.
2. It then assigns the item to every monitor from the real topology.
3. **It switches `Layout` to `independent`.** Assignments are only consumed by
   `StartIndependent`; in the other modes the engine renders the global selection instead.
   Omitting step 3 would make the writes succeed and the desktop stay unchanged — the
   "已应用到桌面" false success this repo has already fixed four times. `PersistMonitorWeb`
   in the Web coordinator has always done both steps for the same reason.
4. If step 3 fails, the call reports failure rather than the success it was about to claim.

The semantic consequence is deliberate and is the literal meaning of a global apply:
per-monitor wallpapers that differ are unified, and the layout becomes Independent. That is
the same trade-off the Web path made, recorded here rather than left implicit.

### A fourth false success, and the shape all four share

The same round found a second one, in the library window rather than in the engine.
`ApplyCallback` returned `void`, so `ApplySelected` had no result to read and left its
`applied` flag at its initialiser, `true`. The engine's `ApplyLibraryItem` bailed out
correctly — it set `libraryError_`, logged, and returned without touching the desktop —
and the window then stamped `MarkUsed`, refreshed, and told the user "已应用到桌面".
`libraryError_` had exactly one reader repo-wide: the advanced settings window's
diagnostics text. Different window, different page, not the one the user was looking at
when they pressed the button. The comment above the call spelled the problem out and
then called it a design.

So the pattern is not any particular bug. It is a **verdict that stops travelling**: the
layer that knows the outcome cannot hand it to the surface that has to speak for it, and
the gap gets papered over with a default that reads as success.

- A `void` callback is a verdict that stops at the boundary. If the callee can fail, the
  return type must be able to say so — an empty string for success, a reason otherwise.
- A diagnostics field with one reader is not user feedback. `libraryError_` was written
  on four paths and displayed in a window nobody opens from the library.
- A comment explaining why a status line "is not evidence of success" is not a mitigation.
  It is the defect, documented in place, where it will be inherited.
- The fix has to reach **every** exit, including the tail: `ApplyLibraryItem` now ends in
  an explicit `return {};`, because falling off a `std::wstring` function is undefined
  behaviour rather than an implicit empty string.

The other half of the same round: when validation refuses a package, the validator's
message is now recorded, shown to the user in the transcript, and fed back into the next
prompt (`BuildPrompt`). Before that the message was discarded — the user got "未检测到有效
内容包路径" and the model got nothing, while its own system prompt told it that scene
`.mdwall` and `.mdwidget` packages are host-validated. A model cannot fix a field it is
never told about.

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
