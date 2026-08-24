# TuringDesk Desktop Composition Architecture

Status: active design for the wallpaper/widget refactor.

Implementation reference: `docs/LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md`

## 1. Product boundary

TuringDesk Desktop is a composition engine, not a single wallpaper renderer.

```text
Desktop Composition
├─ Wallpaper Layer: Image / Video / Web / Scene
├─ Widget Layer: persistent monitor-relative surfaces
└─ Control Layer: Settings / Editor / AI
```

Functional behavior may follow mature desktop-wallpaper conventions, including Wallpaper Engine-class library, monitor, playlist, application-rule, performance and editor workflows. TuringDesk keeps its own branding, assets, package formats and code.

**Wallpaper Engine-class here means product behavior only.** Windows desktop implementation work must use `LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md` as the engineering reference. Lively itself is GPL-3.0, so TuringDesk only studies public behavior/API sequences and independently reimplements them in C++23.

## 2. Runtime composition boundary

The runtime is split into a Shell attachment layer and logical composition layers:

```text
Windows Explorer / Desktop Shell
          ↓
DesktopShellHost
├─ detect Progman / WorkerW / SHELLDLL_DefView
├─ detect Windows 11 raised desktop
├─ attach / validate / repair z-order
└─ recover after Explorer/display rebuild
          ↓
Desktop Composition
├─ Wallpaper Layer
├─ Widget Layer
└─ Runtime diagnostics
```

Wallpaper, Web wallpaper and Widget processes must not each implement their own WorkerW/Progman logic. All desktop surfaces go through the same Shell attachment contract.

## 3. Wallpaper layer

The wallpaper layer remains responsible for filling the desktop surface. The four formal types are:

- Image
- Video
- Web
- Scene

The current native engine keeps monitor topology, performance rules and native media paths. Existing WorkerW/Progman logic is transitional and should be extracted into `DesktopShellHost` rather than expanded inside `WallpaperEngine.cpp`.

New editor/runtime work should be built around typed project state rather than adding more fields directly to `wallpaper.ini`.

## 4. Widget layer

Widgets are independent of the selected wallpaper. Changing a wallpaper must not delete or recreate the user's widget layout.

### Widget v1

The first runtime uses isolated local HTML widgets because the existing WebView2 process model already provides sandboxing, recovery and per-surface bounds.

Each widget stores:

- stable widget id;
- title and kind;
- managed local source;
- stable monitor id, or primary-monitor fallback;
- normalized x/y/width/height coordinates;
- enabled state;
- z-order metadata.

Normalized geometry makes layouts portable across resolution and DPI changes. The runtime converts normalized geometry to the current monitor rectangle on every topology refresh.

Widget v1 is click-through. It must never prevent access to desktop icons. Interactive widgets are a later explicit opt-in mode.

A Widget is not considered running merely because its manifest says `enabled=true`. Runtime state must eventually distinguish:

```text
configured
processStarted
hwndCreated
parentValid
styleValid
zOrderValid
webViewReady
visible
renderingHealthy
lastError
```

### Widget z-order invariant

Logical ordering is:

```text
Desktop icons
─────────────
Widget Layer
─────────────
Wallpaper Layer
─────────────
Windows background / WorkerW
```

The concrete HWND arrangement differs between Legacy WorkerW and Windows 11 Raised Desktop, so the Widget runtime does not directly call `SetParent` based on a guessed hierarchy. It requests attachment from `DesktopShellHost`.

### Future widget kinds

- Native Text
- Clock / Calendar
- Image
- System status
- Media controls
- Web
- Data-bound custom widget

The editor should present these as layer/widget types while preserving one shared property system.

## 5. Control layer

Manual UI and AI must converge on the same control plane.

```text
Settings Center -----------┐
Scene / Widget Editor -----+--> Desktop Control API --> Runtime
Pi Agent ------------------┘
```

Do not create a separate hidden automation implementation for AI. If a property can be changed by the editor, AI should eventually change the same typed property through the same validated mutation path.

### Control API rules

- Read state before mutation.
- Validate every mutation.
- Use stable resource, monitor and widget ids.
- Return the real resulting state.
- Support atomic groups of changes.
- Add undo/redo before AI is allowed to make broad editor mutations.
- Permission-scope data sources and interactive widgets.
- Never let the model claim an applied change without a successful runtime result.
- Runtime success for Web/Widget must include actual surface health, not just a persisted configuration result.

The initial native tools are deliberately narrow: current desktop state, validated Web `.tdwall` apply, and Widget create/update/remove/list. They are the bridge toward the versioned control API, not the final API shape.

## 6. Settings Center target

The desktop settings home should use a familiar wallpaper-library workflow:

```text
Desktop Settings
├─ Installed
├─ Widgets
├─ Playlists
├─ Displays
├─ Application Rules
├─ Performance
└─ General / AI
```

Installed uses real thumbnail cards and a large detail/preview pane. The primary actions are Apply and Edit.

Widgets uses desktop preview plus draggable/resizable widget cards. The same geometry must be editable numerically in the inspector so AI and manual editing share identical state.

The Widgets page must also surface runtime health. `已启用` is configuration state, not proof that a WebView2 surface is actually visible.

## 7. Editor target

Scene and Widget editing share one shell:

```text
┌──────────────┬──────────────────────────┬─────────────────┐
│ Project      │                          │ Inspector       │
│ Layers       │      Live Preview        │ typed props     │
│ Assets       │                          │ bindings        │
├──────────────┴──────────────────────────┴─────────────────┤
│ Timeline / Keyframes / Events                              │
└────────────────────────────────────────────────────────────┘
```

Required typed property kinds:

- bool
- integer / float
- enum
- string
- color
- vector2 / vector3
- resource reference
- curve/easing
- data binding

This schema is also the AI-edit schema.

## 8. Rendering direction

Keep the current native paths where they are strong:

- WIC for image decode;
- Media Foundation for video;
- WebView2 isolated processes for Web;
- native monitor/performance logic.

Refactor Windows Shell integration separately into `DesktopShellHost` using the Lively-informed implementation contract.

Add a modern GPU Scene renderer separately instead of turning the current three hard-coded scenes into an unmaintainable mega-renderer. The old scenes can become built-in projects once the typed scene format exists.

## 9. Package direction

- `.tdwall`: wallpaper project/package.
- `.tdwidget`: widget project/package.

Both should eventually share a safe package core: manifest schema, metadata, version, entry point, permissions, hashes, safe extraction, preview and provenance.

AI-generated packages must be marked with provenance and validated before import/apply.

## 10. Refactor order

1. Extract `DesktopShellHost` and implement Legacy WorkerW / Raised Desktop as separate paths.
2. Reattach Web Wallpaper and Widget through the shared shell host.
3. Add parent/style/z-order/WebView/visible runtime diagnostics.
4. Complete desktop composition model and Widget runtime.
5. Complete AI bridge for state + validated wallpaper/widget mutations.
6. Settings Center library visual rebuild and full Widgets page.
7. Shared typed property schema.
8. Scene/Widget editor shell.
9. GPU effects, particles, audio/mouse response and timeline depth.
10. Transactional Desktop Control API + undo/redo for broad AI editing.

This order preserves working native wallpaper paths while replacing unreliable shell assumptions first.

## 11. Source-of-truth map

```text
Product behavior                 docs/TURINGDESK-PRODUCT-BASELINE.md
Capability backlog               docs/WALLPAPER_ENGINE_PARITY.md
Windows wallpaper implementation docs/LIVELY_CPP_WALLPAPER_IMPLEMENTATION.md
Desktop/Widget composition       docs/DESKTOP_COMPOSITION_ARCHITECTURE.md
AI runtime/tool contract         docs/L3-PI-RUNTIME-CONTRACT.md
```

Do not introduce another parallel WorkerW/Progman design outside this map.
