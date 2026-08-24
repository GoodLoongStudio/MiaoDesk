# TuringDesk Desktop Composition Architecture

Status: active design for the wallpaper/widget refactor.

## 1. Product boundary

TuringDesk Desktop is a composition engine, not a single wallpaper renderer.

```text
Desktop Composition
├─ Wallpaper Layer: Image / Video / Web / Scene
├─ Widget Layer: persistent monitor-relative surfaces
└─ Control Layer: Settings / Editor / AI
```

Functional behavior may follow mature desktop-wallpaper conventions, including Wallpaper Engine-like library, monitor, playlist, application-rule, performance and editor workflows. TuringDesk keeps its own branding, assets, package formats and code.

## 2. Wallpaper layer

The wallpaper layer remains responsible for filling the desktop surface. The four formal types are:

- Image
- Video
- Web
- Scene

The current native engine keeps WorkerW/Progman mounting, monitor topology, performance rules and native media paths. New editor/runtime work should be built around typed project state rather than adding more fields directly to `wallpaper.ini`.

## 3. Widget layer

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

### Future widget kinds

- Native Text
- Clock / Calendar
- Image
- System status
- Media controls
- Web
- Data-bound custom widget

The editor should present these as layer/widget types while preserving one shared property system.

## 4. Control layer

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

The initial native tools are deliberately narrow: current desktop state, validated Web `.tdwall` apply, and Widget create/update/remove/list. They are the bridge toward the versioned control API, not the final API shape.

## 5. Settings Center target

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

## 6. Editor target

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

## 7. Rendering direction

Keep the current native paths where they are strong:

- WIC for image decode;
- Media Foundation for video;
- WebView2 isolated processes for Web;
- native monitor/performance logic.

Add a modern GPU Scene renderer separately instead of turning the current three hard-coded scenes into an unmaintainable mega-renderer. The old scenes can become built-in projects once the typed scene format exists.

## 8. Package direction

- `.tdwall`: wallpaper project/package.
- `.tdwidget`: widget project/package.

Both should eventually share a safe package core: manifest schema, metadata, version, entry point, permissions, hashes, safe extraction, preview and provenance.

AI-generated packages must be marked with provenance and validated before import/apply.

## 9. Refactor order

1. Desktop composition model and Widget runtime.
2. AI bridge for state + validated wallpaper/widget mutations.
3. Settings Center library visual rebuild and first Widgets page.
4. Shared typed property schema.
5. Scene/Widget editor shell.
6. GPU effects, particles, audio/mouse response and timeline depth.
7. Transactional Desktop Control API + undo/redo for broad AI editing.

This order preserves the working wallpaper engine while replacing the user-facing and editing layers incrementally.
