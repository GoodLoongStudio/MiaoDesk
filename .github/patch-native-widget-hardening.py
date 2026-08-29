from pathlib import Path

ROOT = Path('.')

def read(path):
    return (ROOT / path).read_text(encoding='utf-8')

def write(path, text):
    (ROOT / path).write_text(text, encoding='utf-8')

def once(text, old, new, name):
    if old not in text:
        raise SystemExit(f'missing anchor: {name}')
    return text.replace(old, new, 1)

# Store-level singleton guard: never allow a direct service caller to create a
# second built-in native preset on the same monitor and then have it disappear
# during the next legacy-dedup Load().
p = 'src/native/src/desktop/widgets/DesktopWidgetStore.cpp'
s = read(p)
old = '''    const auto index = FindIndex(widget.id);\n    if (index) items_[*index] = widget;\n    else items_.push_back(widget);'''
new = '''    if (widget.kind == DesktopWidgetKind::Native) {\n        const std::wstring singletonKey = NativeSingletonKey(widget);\n        const auto duplicate = std::find_if(items_.begin(), items_.end(), [&](const DesktopWidget& existing) {\n            return _wcsicmp(existing.id.c_str(), widget.id.c_str()) != 0 &&\n                   NativeSingletonKey(existing) == singletonKey;\n        });\n        if (duplicate != items_.end()) {\n            if (error) *error = L"This native widget preset already exists on the target monitor.";\n            return std::nullopt;\n        }\n    }\n\n    const auto index = FindIndex(widget.id);\n    if (index) items_[*index] = widget;\n    else items_.push_back(widget);'''
s = once(s, old, new, 'store singleton upsert guard')

old_test = '''    const auto native = store.CreateManagedNative(\n        NativeWidgetPreset::GlassClock, L"原生时钟", L"monitor-test", 0.2f, 0.3f, 0.25f, 0.18f, &error);\n    ok = ok && native.has_value() && native->kind == DesktopWidgetKind::Native;\n    if (native) ok = ok && store.Remove(native->id, false, &error);'''
new_test = '''    const auto native = store.CreateManagedNative(\n        NativeWidgetPreset::GlassClock, L"原生时钟", L"monitor-test", 0.2f, 0.3f, 0.25f, 0.18f, &error);\n    ok = ok && native.has_value() && native->kind == DesktopWidgetKind::Native;\n    const auto duplicateNative = store.CreateManagedNative(\n        NativeWidgetPreset::GlassClock, L"重复原生时钟", L"monitor-test", 0.4f, 0.2f, 0.25f, 0.18f, &error);\n    ok = ok && !duplicateNative.has_value();\n    if (native) ok = ok && store.Remove(native->id, false, &error);'''
s = once(s, old_test, new_test, 'store singleton selftest')
write(p, s)

# Exact management preview: render using the desktop-sized DIP design canvas and
# scale the full result down uniformly. This preserves the same internal layout
# rather than reflowing the painter against a tiny thumbnail canvas.
p = 'src/native/src/ui/wallpaper/WallpaperLibraryWindowV2.cpp'
s = read(p)
old = '''        FillSolid(dc, bounds, RGB(248, 250, 253));\n        if (FAILED(widgetPreviewTarget->BindDC(dc, &render))) return false;\n        widgetPreviewTarget->SetDpi(96.0f, 96.0f);\n        NativeWidgetPaintContext context{};\n        context.target = widgetPreviewTarget.Get();\n        context.dwrite = widgetPreviewDWrite.Get();\n        context.width = static_cast<float>(std::max(1, RectWidth(render)));\n        context.height = static_cast<float>(std::max(1, RectHeight(render)));\n        context.clearBackground = false;\n        if (preset == NativeWidgetPreset::GlassClock) {\n            GetLocalTime(&context.localTime);\n            context.hasTime = true;\n        }\n        widgetPreviewTarget->BeginDraw();\n        PaintNativeWidgetPreset(context, preset);\n        return SUCCEEDED(widgetPreviewTarget->EndDraw());'''
new = '''        FillSolid(dc, bounds, RGB(248, 250, 253));\n        if (FAILED(widgetPreviewTarget->BindDC(dc, &render))) return false;\n        widgetPreviewTarget->SetDpi(96.0f, 96.0f);\n\n        const UINT desktopDpi = std::max<UINT>(USER_DEFAULT_SCREEN_DPI, GetDpiForWindow(window));\n        const float designW = std::max(1.0f, widget.width * screenW * USER_DEFAULT_SCREEN_DPI / static_cast<float>(desktopDpi));\n        const float designH = std::max(1.0f, widget.height * screenH * USER_DEFAULT_SCREEN_DPI / static_cast<float>(desktopDpi));\n        const float scaleX = static_cast<float>(std::max(1, RectWidth(render))) / designW;\n        const float scaleY = static_cast<float>(std::max(1, RectHeight(render))) / designH;\n        const float scale = std::max(0.01f, std::min(scaleX, scaleY));\n\n        NativeWidgetPaintContext context{};\n        context.target = widgetPreviewTarget.Get();\n        context.dwrite = widgetPreviewDWrite.Get();\n        context.width = designW;\n        context.height = designH;\n        context.clearBackground = false;\n        if (preset == NativeWidgetPreset::GlassClock) {\n            GetLocalTime(&context.localTime);\n            context.hasTime = true;\n        }\n        widgetPreviewTarget->BeginDraw();\n        widgetPreviewTarget->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));\n        PaintNativeWidgetPreset(context, preset);\n        widgetPreviewTarget->SetTransform(D2D1::Matrix3x2F::Identity());\n        return SUCCEEDED(widgetPreviewTarget->EndDraw());'''
s = once(s, old, new, 'uniform native preview scaling')
write(p, s)

print('native widget hardening patch applied')
