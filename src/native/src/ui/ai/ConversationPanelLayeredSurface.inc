namespace turingdesk {
namespace {

struct ConversationLayerSurface {
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ oldBitmap{};
    void* bits{};
    UINT width{};
    UINT height{};
    RECT attachRect{};
    RECT micRect{};
};

ConversationLayerSurface gConversationSurface;

void ReleaseConversationLayerSurface() {
    auto& s = gConversationSurface;
    s.target.Reset();
    s.dwrite.Reset();
    s.d2d.Reset();
    if (s.dc && s.oldBitmap) SelectObject(s.dc, s.oldBitmap);
    if (s.bitmap) DeleteObject(s.bitmap);
    if (s.dc) DeleteDC(s.dc);
    s = {};
}

bool EnsureConversationLayerSurface(UINT width, UINT height) {
    auto& s = gConversationSurface;
    if (!width || !height) return false;
    if (!s.d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, s.d2d.GetAddressOf()))) return false;
    if (!s.dwrite && FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(s.dwrite.GetAddressOf())))) return false;

    if (s.target && s.dc && s.bitmap && s.bits && s.width == width && s.height == height) {
        RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        return SUCCEEDED(s.target->BindDC(s.dc, &bind));
    }

    s.target.Reset();
    if (s.dc && s.oldBitmap) { SelectObject(s.dc, s.oldBitmap); s.oldBitmap = nullptr; }
    if (s.bitmap) { DeleteObject(s.bitmap); s.bitmap = nullptr; }
    if (s.dc) { DeleteDC(s.dc); s.dc = nullptr; }
    s.bits = nullptr;

    s.dc = CreateCompatibleDC(nullptr);
    if (!s.dc) return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    s.bitmap = CreateDIBSection(s.dc, &info, DIB_RGB_COLORS, &s.bits, nullptr, 0);
    if (!s.bitmap || !s.bits) { ReleaseConversationLayerSurface(); return false; }
    s.oldBitmap = SelectObject(s.dc, s.bitmap);
    if (!s.oldBitmap || s.oldBitmap == HGDI_ERROR) { s.oldBitmap = nullptr; ReleaseConversationLayerSurface(); return false; }

    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    if (FAILED(s.d2d->CreateDCRenderTarget(&props, s.target.GetAddressOf()))) {
        ReleaseConversationLayerSurface();
        return false;
    }
    RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (FAILED(s.target->BindDC(s.dc, &bind))) { ReleaseConversationLayerSurface(); return false; }
    s.width = width;
    s.height = height;
    return true;
}

Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> LayerBrush(ID2D1RenderTarget* target, UINT32 rgb, float alpha = 1.0f) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(D2D1::ColorF(rgb, alpha), brush.GetAddressOf());
    return brush;
}

Microsoft::WRL::ComPtr<IDWriteTextFormat> LayerFormat(
    float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING) {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    if (!gConversationSurface.dwrite) return format;
    gConversationSurface.dwrite->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", format.GetAddressOf());
    if (!format) {
        gConversationSurface.dwrite->CreateTextFormat(
            L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", format.GetAddressOf());
    }
    if (format) {
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        format->SetTextAlignment(alignment);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
    return format;
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> LayerLayout(
    const std::wstring& raw, IDWriteTextFormat* format, float width, float height = 4096.0f) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (!format || !gConversationSurface.dwrite) return layout;
    const std::wstring text = CleanMarkdown(raw.empty() ? L" " : raw);
    gConversationSurface.dwrite->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()), format,
        std::max(1.0f, width), std::max(1.0f, height), layout.GetAddressOf());
    return layout;
}

void LayerText(ID2D1RenderTarget* target, const std::wstring& text, IDWriteTextFormat* format,
               ID2D1Brush* brush, float x, float y, float width, float height = 4096.0f) {
    auto layout = LayerLayout(text, format, width, height);
    if (layout) target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float LayerTextWidth(const std::wstring& text, IDWriteTextFormat* format, float maxWidth) {
    auto layout = LayerLayout(text, format, maxWidth);
    if (!layout) return maxWidth;
    DWRITE_TEXT_METRICS metrics{};
    if (FAILED(layout->GetMetrics(&metrics))) return maxWidth;
    return std::min(maxWidth, std::max(1.0f, metrics.widthIncludingTrailingWhitespace));
}

void LayerAvatar(ID2D1RenderTarget* target, float cx, float cy, float radius, bool user) {
    auto outer = LayerBrush(target, user ? 0xF1C7A8 : 0x6B9CFF, 0.98f);
    auto inner = LayerBrush(target, 0xFFFFFF, 0.98f);
    auto core = LayerBrush(target, user ? 0xE5AE87 : 0x806DFF, 0.96f);
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), outer.Get());
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.72f, radius * 0.72f), inner.Get());
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius * 0.38f, radius * 0.38f), core.Get());
}

void LayerPaperclip(ID2D1RenderTarget* target, ID2D1Brush* brush, float x, float y) {
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 5.0f, y - 10.0f, x + 5.0f, y + 10.0f), 5.0f, 5.0f), brush, 1.35f);
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 1.5f, y - 7.0f, x + 2.5f, y + 6.0f), 2.0f, 2.0f), brush, 1.1f);
}

void LayerMic(ID2D1RenderTarget* target, ID2D1Brush* brush, float x, float y) {
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 3.7f, y - 9.0f, x + 3.7f, y + 4.0f), 3.7f, 3.7f), brush, 1.35f);
    target->DrawLine(D2D1::Point2F(x - 7.0f, y + 1.0f), D2D1::Point2F(x - 5.0f, y + 7.0f), brush, 1.35f);
    target->DrawLine(D2D1::Point2F(x + 7.0f, y + 1.0f), D2D1::Point2F(x + 5.0f, y + 7.0f), brush, 1.35f);
    target->DrawLine(D2D1::Point2F(x - 5.0f, y + 7.0f), D2D1::Point2F(x + 5.0f, y + 7.0f), brush, 1.35f);
    target->DrawLine(D2D1::Point2F(x, y + 7.0f), D2D1::Point2F(x, y + 13.0f), brush, 1.35f);
    target->DrawLine(D2D1::Point2F(x - 4.0f, y + 13.0f), D2D1::Point2F(x + 4.0f, y + 13.0f), brush, 1.35f);
}

void LayerHeaderIcon(ID2D1RenderTarget* target, IDWriteTextFormat* format, ID2D1Brush* brush,
                     const RECT& rect, const wchar_t* glyph, bool active = false) {
    if (active) {
        auto activeFill = LayerBrush(target, 0xDDEAFF, 0.72f);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(
            static_cast<float>(rect.left), static_cast<float>(rect.top),
            static_cast<float>(rect.right), static_cast<float>(rect.bottom)), 11.0f, 11.0f), activeFill.Get());
    }
    auto layout = LayerLayout(glyph, format, static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top));
    if (!layout) return;
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    target->DrawTextLayout(D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)), layout.Get(), brush);
}

void LayerTimeline(ID2D1RenderTarget* target, ConversationState& state, const RECT& viewport,
                   IDWriteTextFormat* body, IDWriteTextFormat* bold, IDWriteTextFormat* small,
                   ID2D1Brush* text, ID2D1Brush* secondary) {
    const int viewportWidth = viewport.right - viewport.left;
    state.contentHeight = ComputeContentHeight(gConversationSurface.dc, state, viewportWidth);
    const int maxScroll = std::max(0, state.contentHeight - (viewport.bottom - viewport.top));
    if (state.scrollToBottom) { state.scrollOffset = maxScroll; state.scrollToBottom = false; }
    state.scrollOffset = std::clamp(state.scrollOffset, 0, maxScroll);

    target->PushAxisAlignedClip(D2D1::RectF(
        static_cast<float>(viewport.left), static_cast<float>(viewport.top),
        static_cast<float>(viewport.right), static_cast<float>(viewport.bottom)), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    const float avatar = static_cast<float>(Px(state, 30));
    const float avatarR = avatar * 0.5f;
    const float gap = static_cast<float>(Px(state, 10));
    const float maxBubble = std::max(static_cast<float>(Px(state, 180)), viewportWidth * 0.72f);
    auto aiFill = LayerBrush(target, 0xFFFFFF, 0.86f);
    auto userFill = LayerBrush(target, 0xD8E9FF, 0.88f);
    auto border = LayerBrush(target, 0xD7E0ED, 0.82f);
    auto shadow = LayerBrush(target, 0x8290A5, 0.12f);
    auto systemFill = LayerBrush(target, 0xE8F4EB, 0.82f);
    float y = static_cast<float>(viewport.top + Px(state, 8) - state.scrollOffset);

    for (const auto& entry : state.entries) {
        const int rowH = EntryHeight(gConversationSurface.dc, state, entry, viewportWidth);
        if (entry.kind == EntryKind::System) {
            const auto note = D2D1::RoundedRect(D2D1::RectF(
                viewport.left + static_cast<float>(Px(state, 28)), y + Px(state, 4),
                viewport.right - static_cast<float>(Px(state, 28)), y + rowH - Px(state, 4)),
                static_cast<float>(Px(state, 13)), static_cast<float>(Px(state, 13)));
            target->FillRoundedRectangle(note, systemFill.Get());
            LayerText(target, entry.text, small, secondary,
                      note.rect.left + Px(state, 14), note.rect.top + Px(state, 9),
                      note.rect.right - note.rect.left - Px(state, 28), note.rect.bottom - note.rect.top - Px(state, 18));
            y += rowH + gap;
            continue;
        }

        const bool user = entry.kind == EntryKind::User;
        const std::wstring display = entry.text.empty() ? L"正在思考…" : entry.text;
        const float textWidth = LayerTextWidth(display, body, maxBubble - Px(state, 32));
        const float bubbleW = std::clamp(textWidth + Px(state, 32.0f), static_cast<float>(Px(state, 150)), maxBubble);
        const float bubbleH = static_cast<float>(std::max(Px(state, 54), rowH - Px(state, 2)));
        const float avatarX = user ? viewport.right - avatarR : viewport.left + avatarR;
        const float bubbleLeft = user
            ? viewport.right - avatar - Px(state, 10) - bubbleW
            : viewport.left + avatar + Px(state, 10);
        const auto bubble = D2D1::RoundedRect(D2D1::RectF(
            bubbleLeft, y, bubbleLeft + bubbleW, y + bubbleH),
            static_cast<float>(Px(state, 15)), static_cast<float>(Px(state, 15)));
        const auto bubbleShadow = D2D1::RoundedRect(D2D1::RectF(
            bubble.rect.left + 1.5f, bubble.rect.top + 2.5f,
            bubble.rect.right + 1.5f, bubble.rect.bottom + 2.5f), bubble.radiusX, bubble.radiusY);
        target->FillRoundedRectangle(bubbleShadow, shadow.Get());
        target->FillRoundedRectangle(bubble, user ? userFill.Get() : aiFill.Get());
        target->DrawRoundedRectangle(bubble, border.Get(), 0.8f);
        LayerAvatar(target, avatarX, y + avatarR + Px(state, 2), avatarR, user);
        LayerText(target, display, body, text,
                  bubble.rect.left + Px(state, 16), bubble.rect.top + Px(state, 11),
                  bubbleW - Px(state, 32), bubbleH - Px(state, 28));
        auto timeLayout = LayerLayout(entry.time, small, bubbleW - Px(state, 28), Px(state, 18));
        if (timeLayout) {
            timeLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            target->DrawTextLayout(D2D1::Point2F(bubble.rect.left + Px(state, 14), bubble.rect.bottom - Px(state, 19)), timeLayout.Get(), secondary);
        }
        y += rowH + gap;
    }

    if (state.activity.visible) {
        const float cardW = viewportWidth * 0.76f;
        const float cardH = static_cast<float>(ActivityHeight(state));
        const float left = viewport.left + avatar + Px(state, 10);
        auto fill = LayerBrush(target, state.activity.failed ? 0xFFF5F2 : 0xFBFDFF, 0.92f);
        auto outline = LayerBrush(target, state.activity.failed ? 0xF0C3BB : 0xDCE5F1, 0.90f);
        const auto card = D2D1::RoundedRect(D2D1::RectF(left, y, left + cardW, y + cardH), Px(state, 16.0f), Px(state, 16.0f));
        target->FillRoundedRectangle(card, fill.Get());
        target->DrawRoundedRectangle(card, outline.Get(), 0.85f);
        LayerText(target, state.activity.title, bold, text, left + Px(state, 18), y + Px(state, 13), cardW - Px(state, 36), Px(state, 26));
        LayerText(target, state.activity.detail, small, secondary, left + Px(state, 18), y + Px(state, 39), cardW - Px(state, 36), Px(state, 20));
        float stepY = y + Px(state, 64);
        for (const auto& step : state.activity.steps) {
            const UINT32 color = step.status == StepStatus::Failed ? 0xDB4D41 : step.status == StepStatus::Success ? 0x56B86D : 0x5B8BF4;
            auto dot = LayerBrush(target, color, 0.96f);
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(left + Px(state, 20), stepY + Px(state, 6)), Px(state, 4.0f), Px(state, 4.0f)), dot.Get());
            const std::wstring prefix = step.status == StepStatus::Success ? L"已完成 · " : step.status == StepStatus::Failed ? L"失败 · " : L"进行中 · ";
            LayerText(target, prefix + step.label, small, secondary, left + Px(state, 32), stepY - Px(state, 4), cardW - Px(state, 50), Px(state, 22));
            stepY += Px(state, 25);
        }
        y += cardH + Px(state, 12);
    }

    if (state.pendingConfirmation) {
        const float cardW = viewportWidth * 0.78f;
        const float cardH = static_cast<float>(ConfirmationHeight(state));
        const float left = viewport.left + avatar + Px(state, 10);
        auto fill = LayerBrush(target, 0xFFF9F1, 0.95f);
        auto outline = LayerBrush(target, 0xEFCF9E, 0.92f);
        const auto card = D2D1::RoundedRect(D2D1::RectF(left, y, left + cardW, y + cardH), Px(state, 16.0f), Px(state, 16.0f));
        target->FillRoundedRectangle(card, fill.Get());
        target->DrawRoundedRectangle(card, outline.Get(), 0.85f);
        LayerText(target, L"需要你确认", bold, text, left + Px(state, 18), y + Px(state, 13), cardW - Px(state, 36), Px(state, 26));
        LayerText(target, L"这项操作可能删除、覆盖或改变本机内容。确认后图灵才会继续。", small, secondary,
                  left + Px(state, 18), y + Px(state, 42), cardW - Px(state, 36), Px(state, 38));
        const int buttonW = Px(state, 92), buttonH = Px(state, 34);
        state.confirmRect = {static_cast<LONG>(left + cardW - buttonW - Px(state, 16)), static_cast<LONG>(y + cardH - buttonH - Px(state, 14)), static_cast<LONG>(left + cardW - Px(state, 16)), static_cast<LONG>(y + cardH - Px(state, 14))};
        state.cancelRect = {state.confirmRect.left - buttonW - Px(state, 10), state.confirmRect.top, state.confirmRect.left - Px(state, 10), state.confirmRect.bottom};
        auto cancelFill = LayerBrush(target, 0xEEF1F5, 0.96f);
        auto confirmFill = LayerBrush(target, 0xED684E, 0.96f);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(state.cancelRect.left, state.cancelRect.top, state.cancelRect.right, state.cancelRect.bottom), Px(state, 10.0f), Px(state, 10.0f)), cancelFill.Get());
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(state.confirmRect.left, state.confirmRect.top, state.confirmRect.right, state.confirmRect.bottom), Px(state, 10.0f), Px(state, 10.0f)), confirmFill.Get());
        LayerText(target, L"取消", small, text, state.cancelRect.left + Px(state, 30.0f), state.cancelRect.top + Px(state, 8.0f), buttonW - Px(state, 20.0f), buttonH);
        auto white = LayerBrush(target, 0xFFFFFF, 1.0f);
        LayerText(target, L"确认执行", small, white.Get(), state.confirmRect.left + Px(state, 20.0f), state.confirmRect.top + Px(state, 8.0f), buttonW - Px(state, 20.0f), buttonH);
    }

    target->PopAxisAlignedClip();

    if (maxScroll > 0) {
        const float trackTop = viewport.top + Px(state, 8.0f);
        const float trackBottom = viewport.bottom - Px(state, 8.0f);
        const float trackH = trackBottom - trackTop;
        const float thumbH = std::max(static_cast<float>(Px(state, 32)), trackH * (viewport.bottom - viewport.top) / std::max(1, state.contentHeight));
        const float travel = std::max(1.0f, trackH - thumbH);
        const float thumbTop = trackTop + travel * state.scrollOffset / maxScroll;
        auto thumb = LayerBrush(target, 0x9DA9BB, 0.60f);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(viewport.right - Px(state, 4.0f), thumbTop, viewport.right - Px(state, 1.0f), thumbTop + thumbH), 2.0f, 2.0f), thumb.Get());
    }
}

bool PresentConversationLayerSurface(HWND hwnd, UINT width, UINT height) {
    auto& s = gConversationSurface;
    RECT wr{};
    if (!GetWindowRect(hwnd, &wr)) return false;
    POINT destination{wr.left, wr.top};
    POINT source{0, 0};
    SIZE size{static_cast<LONG>(width), static_cast<LONG>(height)};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    return UpdateLayeredWindow(hwnd, nullptr, &destination, &size, s.dc, &source, 0, &blend, ULW_ALPHA) != FALSE;
}

bool RenderConversationLayerSurface(ConversationState& state) {
    RECT client{};
    if (!state.window || !GetClientRect(state.window, &client)) return false;
    const UINT width = static_cast<UINT>(std::max<LONG>(0, client.right));
    const UINT height = static_cast<UINT>(std::max<LONG>(0, client.bottom));
    if (!EnsureConversationLayerSurface(width, height)) return false;

    auto& s = gConversationSurface;
    std::memset(s.bits, 0, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (FAILED(s.target->BindDC(s.dc, &bind))) return false;

    auto* target = s.target.Get();
    target->BeginDraw();
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->Clear(D2D1::ColorF(0x000000, 0.0f));

    const float w = static_cast<float>(width), h = static_cast<float>(height);
    auto panelFill = LayerBrush(target, 0xEEF4FC, 0.86f);
    auto headerFill = LayerBrush(target, 0xF8FAFD, 0.64f);
    auto outline = LayerBrush(target, 0xFFFFFF, 0.82f);
    auto divider = LayerBrush(target, 0xAAB9CE, 0.30f);
    auto text = LayerBrush(target, 0x29303D, 0.98f);
    auto secondary = LayerBrush(target, 0x68768C, 0.94f);
    auto inputFill = LayerBrush(target, 0xFFFFFF, 0.78f);
    auto inputOutline = LayerBrush(target, 0xD5DFEC, 0.84f);

    const float radius = static_cast<float>(Px(state, 24));
    const auto panel = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), radius, radius);
    target->FillRoundedRectangle(panel, panelFill.Get());
    target->DrawRoundedRectangle(panel, outline.Get(), 1.0f);

    const float headerH = static_cast<float>(Px(state, 70));
    target->PushAxisAlignedClip(D2D1::RectF(0.5f, 0.5f, w - 0.5f, headerH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->FillRoundedRectangle(panel, headerFill.Get());
    target->PopAxisAlignedClip();
    target->DrawLine(D2D1::Point2F(0.0f, headerH - 0.5f), D2D1::Point2F(w, headerH - 0.5f), divider.Get(), 1.0f);

    auto titleFormat = LayerFormat(static_cast<float>(Px(state, 20)), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto bodyFormat = LayerFormat(static_cast<float>(Px(state, 15)));
    auto boldFormat = LayerFormat(static_cast<float>(Px(state, 15)), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto smallFormat = LayerFormat(static_cast<float>(Px(state, 12)));
    auto symbolFormat = LayerFormat(static_cast<float>(Px(state, 17)), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);

    LayerAvatar(target, static_cast<float>(Px(state, 34)), static_cast<float>(Px(state, 34)), static_cast<float>(Px(state, 17)), false);
    LayerText(target, L"妙喵 AI", titleFormat.Get(), text.Get(), Px(state, 60.0f), Px(state, 14.0f), Px(state, 180.0f), Px(state, 28.0f));
    auto online = LayerBrush(target, 0x53BE68, 0.98f);
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(Px(state, 63.0f), Px(state, 50.0f)), Px(state, 4.0f), Px(state, 4.0f)), online.Get());
    LayerText(target, state.busy ? L"正在工作" : L"在线", smallFormat.Get(), secondary.Get(), Px(state, 74.0f), Px(state, 40.0f), Px(state, 120.0f), Px(state, 20.0f));

    const int iconW = Px(state, 34), iconTop = Px(state, 17);
    state.closeRect = {static_cast<LONG>(width) - Px(state, 48), iconTop, static_cast<LONG>(width) - Px(state, 14), iconTop + iconW};
    state.moreRect = {state.closeRect.left - Px(state, 42), iconTop, state.closeRect.left - Px(state, 8), iconTop + iconW};
    state.historyRect = {state.moreRect.left - Px(state, 42), iconTop, state.moreRect.left - Px(state, 8), iconTop + iconW};
    state.pinRect = {state.historyRect.left - Px(state, 42), iconTop, state.historyRect.left - Px(state, 8), iconTop + iconW};
    LayerHeaderIcon(target, symbolFormat.Get(), secondary.Get(), state.pinRect, L"⌖", state.pinned);
    LayerHeaderIcon(target, symbolFormat.Get(), secondary.Get(), state.historyRect, L"↶");
    LayerHeaderIcon(target, symbolFormat.Get(), secondary.Get(), state.moreRect, L"•••");
    LayerHeaderIcon(target, symbolFormat.Get(), secondary.Get(), state.closeRect, L"×");

    const int inputContainerH = Px(state, 64);
    const int inputTop = static_cast<int>(height) - inputContainerH - Px(state, 16);
    state.viewport = {Px(state, 22), static_cast<LONG>(headerH) + Px(state, 6), static_cast<LONG>(width) - Px(state, 22), inputTop - Px(state, 10)};
    LayerTimeline(target, state, state.viewport, bodyFormat.Get(), boldFormat.Get(), smallFormat.Get(), text.Get(), secondary.Get());

    const auto inputBox = D2D1::RoundedRect(D2D1::RectF(
        static_cast<float>(Px(state, 22)), static_cast<float>(inputTop), w - Px(state, 22.0f), static_cast<float>(inputTop + inputContainerH)),
        static_cast<float>(Px(state, 20)), static_cast<float>(Px(state, 20)));
    target->FillRoundedRectangle(inputBox, inputFill.Get());
    target->DrawRoundedRectangle(inputBox, inputOutline.Get(), 0.9f);

    const int sendSize = Px(state, 42);
    state.sendRect = {static_cast<LONG>(width) - Px(state, 22) - sendSize - Px(state, 10), inputTop + Px(state, 11), static_cast<LONG>(width) - Px(state, 32), inputTop + Px(state, 11) + sendSize};
    auto sendFill = LayerBrush(target, state.busy ? 0xC7D2E6 : 0x5B8BF4, 0.98f);
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(
        (state.sendRect.left + state.sendRect.right) * 0.5f,
        (state.sendRect.top + state.sendRect.bottom) * 0.5f), sendSize * 0.5f, sendSize * 0.5f), sendFill.Get());
    auto white = LayerBrush(target, 0xFFFFFF, 1.0f);
    if (state.busy) {
        target->FillRectangle(D2D1::RectF(state.sendRect.left + Px(state, 16.0f), state.sendRect.top + Px(state, 16.0f), state.sendRect.right - Px(state, 16.0f), state.sendRect.bottom - Px(state, 16.0f)), white.Get());
    } else {
        const float cx = (state.sendRect.left + state.sendRect.right) * 0.5f;
        const float cy = (state.sendRect.top + state.sendRect.bottom) * 0.5f;
        target->DrawLine(D2D1::Point2F(cx, cy + Px(state, 7.0f)), D2D1::Point2F(cx, cy - Px(state, 7.0f)), white.Get(), 1.8f);
        target->DrawLine(D2D1::Point2F(cx, cy - Px(state, 7.0f)), D2D1::Point2F(cx - Px(state, 5.0f), cy - Px(state, 2.0f)), white.Get(), 1.8f);
        target->DrawLine(D2D1::Point2F(cx, cy - Px(state, 7.0f)), D2D1::Point2F(cx + Px(state, 5.0f), cy - Px(state, 2.0f)), white.Get(), 1.8f);
    }

    const int actionSize = Px(state, 34), actionGap = Px(state, 4);
    const int micLeft = state.sendRect.left - Px(state, 8) - actionSize;
    const int attachLeft = micLeft - actionGap - actionSize;
    const int actionTop = inputTop + Px(state, 15);
    s.attachRect = {attachLeft, actionTop, attachLeft + actionSize, actionTop + actionSize};
    s.micRect = {micLeft, actionTop, micLeft + actionSize, actionTop + actionSize};
    LayerPaperclip(target, secondary.Get(), attachLeft + actionSize * 0.5f, actionTop + actionSize * 0.5f);
    LayerMic(target, secondary.Get(), micLeft + actionSize * 0.5f, actionTop + actionSize * 0.5f - Px(state, 1.0f));

    const std::wstring inputText = ReadText(state.input);
    const float inputLeft = static_cast<float>(Px(state, 38));
    const float inputRight = static_cast<float>(attachLeft - Px(state, 8));
    if (inputText.empty()) LayerText(target, L"输入消息…", bodyFormat.Get(), secondary.Get(), inputLeft, inputTop + Px(state, 19.0f), inputRight - inputLeft, Px(state, 30.0f));
    else LayerText(target, inputText, bodyFormat.Get(), text.Get(), inputLeft, inputTop + Px(state, 16.0f), inputRight - inputLeft, Px(state, 36.0f));

    auto footer = LayerLayout(L"Enter 发送 · Shift + Enter 换行 · Esc 停止 / 收起", smallFormat.Get(), w - Px(state, 80.0f), Px(state, 18.0f));
    if (footer) {
        footer->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        target->DrawTextLayout(D2D1::Point2F(Px(state, 40.0f), h - Px(state, 16.0f)), footer.Get(), secondary.Get());
    }

    const HRESULT result = target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) { ReleaseConversationLayerSurface(); return false; }
    if (FAILED(result)) return false;

    RECT wr{};
    if (!GetWindowRect(state.window, &wr)) return false;
    POINT destination{wr.left, wr.top};
    POINT source{0, 0};
    SIZE size{static_cast<LONG>(width), static_cast<LONG>(height)};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    return UpdateLayeredWindow(state.window, nullptr, &destination, &size, s.dc, &source, 0, &blend, ULW_ALPHA) != FALSE;
}

void PositionConversationInputProxy(ConversationState& state) {
    if (state.input) MoveWindow(state.input, Px(state, 38), Px(state, 1), 1, 1, FALSE);
}

bool ConversationPointInsideRoundedPanel(HWND hwnd, POINT screenPoint, int radius) {
    RECT wr{};
    if (!GetWindowRect(hwnd, &wr)) return true;
    const int width = wr.right - wr.left, height = wr.bottom - wr.top;
    const int x = screenPoint.x - wr.left, y = screenPoint.y - wr.top;
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    if ((x >= radius && x < width - radius) || (y >= radius && y < height - radius)) return true;
    const int cx = x < radius ? radius : width - radius - 1;
    const int cy = y < radius ? radius : height - radius - 1;
    const int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

} // namespace
} // namespace turingdesk
