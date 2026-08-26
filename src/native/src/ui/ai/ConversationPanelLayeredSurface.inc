namespace turingdesk {
namespace {

struct ConversationLayeredSurface {
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

ConversationLayeredSurface gConversationLayered;

void ReleaseConversationLayeredSurface() {
    auto& surface = gConversationLayered;
    surface.target.Reset();
    surface.dwrite.Reset();
    surface.d2d.Reset();
    if (surface.dc && surface.oldBitmap) {
        SelectObject(surface.dc, surface.oldBitmap);
        surface.oldBitmap = nullptr;
    }
    if (surface.bitmap) {
        DeleteObject(surface.bitmap);
        surface.bitmap = nullptr;
    }
    if (surface.dc) {
        DeleteDC(surface.dc);
        surface.dc = nullptr;
    }
    surface.bits = nullptr;
    surface.width = 0;
    surface.height = 0;
    surface.attachRect = {};
    surface.micRect = {};
}

bool EnsureConversationLayeredSurface(UINT width, UINT height) {
    auto& surface = gConversationLayered;
    if (width == 0 || height == 0) return false;

    if (!surface.d2d && FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED, surface.d2d.GetAddressOf()))) return false;
    if (!surface.dwrite && FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(surface.dwrite.GetAddressOf())))) return false;

    if (surface.target && surface.dc && surface.bitmap && surface.bits &&
        surface.width == width && surface.height == height) {
        RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        return SUCCEEDED(surface.target->BindDC(surface.dc, &bind));
    }

    surface.target.Reset();
    if (surface.dc && surface.oldBitmap) {
        SelectObject(surface.dc, surface.oldBitmap);
        surface.oldBitmap = nullptr;
    }
    if (surface.bitmap) { DeleteObject(surface.bitmap); surface.bitmap = nullptr; }
    if (surface.dc) { DeleteDC(surface.dc); surface.dc = nullptr; }
    surface.bits = nullptr;

    surface.dc = CreateCompatibleDC(nullptr);
    if (!surface.dc) return false;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    surface.bitmap = CreateDIBSection(surface.dc, &info, DIB_RGB_COLORS, &surface.bits, nullptr, 0);
    if (!surface.bitmap || !surface.bits) {
        ReleaseConversationLayeredSurface();
        return false;
    }
    surface.oldBitmap = SelectObject(surface.dc, surface.bitmap);
    if (!surface.oldBitmap || surface.oldBitmap == HGDI_ERROR) {
        surface.oldBitmap = nullptr;
        ReleaseConversationLayeredSurface();
        return false;
    }

    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    if (FAILED(surface.d2d->CreateDCRenderTarget(&props, surface.target.GetAddressOf()))) {
        ReleaseConversationLayeredSurface();
        return false;
    }
    RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (FAILED(surface.target->BindDC(surface.dc, &bind))) {
        ReleaseConversationLayeredSurface();
        return false;
    }
    surface.width = width;
    surface.height = height;
    return true;
}

Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ConversationBrush(
    ID2D1RenderTarget* target, UINT32 rgb, float alpha = 1.0f) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(D2D1::ColorF(rgb, alpha), brush.GetAddressOf());
    return brush;
}

Microsoft::WRL::ComPtr<IDWriteTextFormat> ConversationFormat(
    float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING) {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    auto& surface = gConversationLayered;
    if (!surface.dwrite) return format;
    surface.dwrite->CreateTextFormat(
        L"Segoe UI Variable Text", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", format.GetAddressOf());
    if (!format) {
        surface.dwrite->CreateTextFormat(
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

Microsoft::WRL::ComPtr<IDWriteTextLayout> ConversationLayout(
    const std::wstring& raw, IDWriteTextFormat* format, float width, float height = 10000.0f) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (!gConversationLayered.dwrite || !format) return layout;
    const std::wstring text = CleanMarkdown(raw.empty() ? L" " : raw);
    gConversationLayered.dwrite->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()), format,
        std::max(1.0f, width), std::max(1.0f, height), layout.GetAddressOf());
    return layout;
}

DWRITE_TEXT_METRICS ConversationMetrics(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics{};
    if (layout) layout->GetMetrics(&metrics);
    return metrics;
}

void DrawConversationText(
    ID2D1RenderTarget* target, const std::wstring& text, IDWriteTextFormat* format,
    ID2D1Brush* brush, float x, float y, float width, float height = 10000.0f) {
    auto layout = ConversationLayout(text, format, width, height);
    if (layout) target->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void DrawConversationAvatar(ID2D1RenderTarget* target, float cx, float cy, float r, bool user) {
    auto outer = ConversationBrush(target, user ? 0xF3C9AB : 0x6B9CFF, 0.98f);
    auto inner = ConversationBrush(target, 0xFFFFFF, 0.98f);
    auto core = ConversationBrush(target, user ? 0xE9B38E : 0x806DFF, 0.96f);
    if (!outer || !inner || !core) return;
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), outer.Get());
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.72f, r * 0.72f), inner.Get());
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.40f, r * 0.40f), core.Get());
}

void DrawConversationMic(ID2D1RenderTarget* target, ID2D1Brush* brush, float x, float y) {
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 3.6f, y - 8.0f, x + 3.6f, y + 4.0f), 3.6f, 3.6f), brush, 1.4f);
    target->DrawArc(D2D1::Ellipse(D2D1::Point2F(x, y + 2.0f), 7.0f, 7.0f), brush, 1.4f);
    target->DrawLine(D2D1::Point2F(x, y + 9.0f), D2D1::Point2F(x, y + 14.0f), brush, 1.4f);
    target->DrawLine(D2D1::Point2F(x - 4.0f, y + 14.0f), D2D1::Point2F(x + 4.0f, y + 14.0f), brush, 1.4f);
}

void DrawConversationPaperclip(ID2D1RenderTarget* target, ID2D1Brush* brush, float x, float y) {
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 5.0f, y - 10.0f, x + 5.0f, y + 10.0f), 5.0f, 5.0f), brush, 1.35f);
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 1.5f, y - 7.0f, x + 2.5f, y + 6.5f), 2.0f, 2.0f), brush, 1.15f);
}

void DrawConversationHeaderIcon(
    ID2D1RenderTarget* target, IDWriteTextFormat* symbolFormat, ID2D1Brush* brush,
    const RECT& rect, const wchar_t* glyph, bool active = false) {
    if (active) {
        auto activeFill = ConversationBrush(target, 0xDDEAFF, 0.72f);
        target->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                        static_cast<float>(rect.right), static_cast<float>(rect.bottom)), 11.0f, 11.0f), activeFill.Get());
    }
    auto layout = ConversationLayout(glyph, symbolFormat,
        static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top));
    if (layout) {
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        target->DrawTextLayout(D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)), layout.Get(), brush);
    }
}

int DrawConversationTimeline(
    ID2D1RenderTarget* target, ConversationState& state, const RECT& viewport,
    IDWriteTextFormat* bodyFormat, IDWriteTextFormat* bodyBoldFormat,
    IDWriteTextFormat* smallFormat, ID2D1Brush* textBrush, ID2D1Brush* secondaryBrush) {
    const float viewportW = static_cast<float>(viewport.right - viewport.left);
    const float avatar = static_cast<float>(Px(state, 30));
    const float gap = static_cast<float>(Px(state, 10));
    const float maxBubble = std::max(static_cast<float>(Px(state, 180)), viewportW * 0.72f);

    struct Row { const ChatEntry* entry{}; float height{}; float bubbleW{}; float bubbleH{}; Microsoft::WRL::ComPtr<IDWriteTextLayout> layout; };
    std::vector<Row> rows;
    rows.reserve(state.entries.size());
    float content = static_cast<float>(Px(state, 8));
    for (const auto& entry : state.entries) {
        if (entry.kind == EntryKind::System) {
            rows.push_back(Row{&entry, static_cast<float>(Px(state, 52))});
            content += rows.back().height + gap;
            continue;
        }
        const std::wstring display = entry.text.empty() ? L"正在思考…" : entry.text;
        auto layout = ConversationLayout(display, bodyFormat, maxBubble - Px(state, 32));
        const auto metrics = ConversationMetrics(layout.Get());
        const float bubbleW = std::clamp(metrics.widthIncludingTrailingWhitespace + Px(state, 32.0f),
                                         static_cast<float>(Px(state, 150)), maxBubble);
        const float bubbleH = std::max(static_cast<float>(Px(state, 54)), metrics.height + Px(state, 34.0f));
        const float rowH = std::max(avatar + Px(state, 10.0f), bubbleH);
        rows.push_back(Row{&entry, rowH, bubbleW, bubbleH, std::move(layout)});
        content += rowH + gap;
    }
    if (state.activity.visible) content += static_cast<float>(ActivityHeight(state) + Px(state, 12));
    if (state.pendingConfirmation) content += static_cast<float>(ConfirmationHeight(state) + Px(state, 12));
    state.contentHeight = static_cast<int>(std::ceil(content));
    const int viewportH = viewport.bottom - viewport.top;
    const int maxScroll = std::max(0, state.contentHeight - viewportH);
    if (state.scrollToBottom) { state.scrollOffset = maxScroll; state.scrollToBottom = false; }
    state.scrollOffset = std::clamp(state.scrollOffset, 0, maxScroll);

    target->PushAxisAlignedClip(D2D1::RectF(
        static_cast<float>(viewport.left), static_cast<float>(viewport.top),
        static_cast<float>(viewport.right), static_cast<float>(viewport.bottom)), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float y = static_cast<float>(viewport.top + Px(state, 8) - state.scrollOffset);
    const float avatarR = avatar * 0.5f;
    auto aiFill = ConversationBrush(target, 0xFFFFFF, 0.86f);
    auto userFill = ConversationBrush(target, 0xD9E9FF, 0.88f);
    auto cardOutline = ConversationBrush(target, 0xD7E0ED, 0.78f);
    auto shadow = ConversationBrush(target, 0x8290A5, 0.12f);
    auto systemFill = ConversationBrush(target, 0xE8F4EB, 0.80f);

    for (const auto& row : rows) {
        const auto& entry = *row.entry;
        if (entry.kind == EntryKind::System) {
            const auto note = D2D1::RoundedRect(D2D1::RectF(
                viewport.left + Px(state, 28.0f), y + Px(state, 4.0f),
                viewport.right - Px(state, 28.0f), y + row.height - Px(state, 4.0f)),
                Px(state, 13.0f), Px(state, 13.0f));
            target->FillRoundedRectangle(note, systemFill.Get());
            DrawConversationText(target, entry.text, smallFormat, secondaryBrush,
                note.rect.left + Px(state, 14.0f), note.rect.top + Px(state, 9.0f),
                note.rect.right - note.rect.left - Px(state, 28.0f), note.rect.bottom - note.rect.top - Px(state, 18.0f));
            y += row.height + gap;
            continue;
        }

        const bool user = entry.kind == EntryKind::User;
        const float avatarX = user ? viewport.right - avatarR : viewport.left + avatarR;
        const float bubbleLeft = user
            ? viewport.right - avatar - Px(state, 10.0f) - row.bubbleW
            : viewport.left + avatar + Px(state, 10.0f);
        const auto bubble = D2D1::RoundedRect(D2D1::RectF(
            bubbleLeft, y, bubbleLeft + row.bubbleW, y + row.bubbleH), Px(state, 15.0f), Px(state, 15.0f));
        const auto bubbleShadow = D2D1::RoundedRect(D2D1::RectF(
            bubble.rect.left + 1.5f, bubble.rect.top + 2.5f,
            bubble.rect.right + 1.5f, bubble.rect.bottom + 2.5f), bubble.radiusX, bubble.radiusY);
        target->FillRoundedRectangle(bubbleShadow, shadow.Get());
        target->FillRoundedRectangle(bubble, user ? userFill.Get() : aiFill.Get());
        target->DrawRoundedRectangle(bubble, cardOutline.Get(), 0.8f);
        DrawConversationAvatar(target, avatarX, y + avatarR + Px(state, 2.0f), avatarR, user);
        if (row.layout) target->DrawTextLayout(
            D2D1::Point2F(bubble.rect.left + Px(state, 16.0f), bubble.rect.top + Px(state, 11.0f)),
            row.layout.Get(), textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        DrawConversationText(target, entry.time, smallFormat, secondaryBrush,
            bubble.rect.left + Px(state, 16.0f), bubble.rect.bottom - Px(state, 19.0f),
            row.bubbleW - Px(state, 28.0f), Px(state, 16.0f));
        y += row.height + gap;
    }

    if (state.activity.visible) {
        const float cardW = viewportW * 0.76f;
        const float cardH = static_cast<float>(ActivityHeight(state));
        const float left = viewport.left + avatar + Px(state, 10.0f);
        auto fill = ConversationBrush(target, state.activity.failed ? 0xFFF5F2 : 0xFBFDFF, 0.92f);
        auto outline = ConversationBrush(target, state.activity.failed ? 0xF0C3BB : 0xDCE5F1, 0.90f);
        const auto card = D2D1::RoundedRect(D2D1::RectF(left, y, left + cardW, y + cardH), Px(state, 16.0f), Px(state, 16.0f));
        target->FillRoundedRectangle(card, fill.Get());
        target->DrawRoundedRectangle(card, outline.Get(), 0.85f);
        DrawConversationText(target, state.activity.title, bodyBoldFormat, textBrush,
            left + Px(state, 18.0f), y + Px(state, 13.0f), cardW - Px(state, 36.0f), Px(state, 25.0f));
        DrawConversationText(target, state.activity.detail, smallFormat, secondaryBrush,
            left + Px(state, 18.0f), y + Px(state, 39.0f), cardW - Px(state, 36.0f), Px(state, 20.0f));
        float sy = y + Px(state, 64.0f);
        for (const auto& step : state.activity.steps) {
            const UINT32 dotColor = step.status == StepStatus::Failed ? 0xDB4D41 : step.status == StepStatus::Success ? 0x56B86D : 0x5B8BF4;
            auto dot = ConversationBrush(target, dotColor, 0.96f);
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(left + Px(state, 20.0f), sy + Px(state, 6.0f)), Px(state, 4.0f), Px(state, 4.0f)), dot.Get());
            const std::wstring prefix = step.status == StepStatus::Success ? L"已完成 · " : step.status == StepStatus::Failed ? L"失败 · " : L"进行中 · ";
            DrawConversationText(target, prefix + step.label, smallFormat, secondaryBrush,
                left + Px(state, 32.0f), sy - Px(state, 4.0f), cardW - Px(state, 50.0f), Px(state, 22.0f));
            sy += Px(state, 25.0f);
        }
        y += cardH + Px(state, 12.0f);
    }

    if (state.pendingConfirmation) {
        const float cardW = viewportW * 0.78f;
        const float cardH = static_cast<float>(ConfirmationHeight(state));
        const float left = viewport.left + avatar + Px(state, 10.0f);
        auto fill = ConversationBrush(target, 0xFFF9F1, 0.95f);
        auto outline = ConversationBrush(target, 0xEFCF9E, 0.92f);
        const auto card = D2D1::RoundedRect(D2D1::RectF(left, y, left + cardW, y + cardH), Px(state, 16.0f), Px(state, 16.0f));
        target->FillRoundedRectangle(card, fill.Get());
        target->DrawRoundedRectangle(card, outline.Get(), 0.85f);
        DrawConversationText(target, L"需要你确认", bodyBoldFormat, textBrush,
            left + Px(state, 18.0f), y + Px(state, 13.0f), cardW - Px(state, 36.0f), Px(state, 25.0f));
        DrawConversationText(target, L"这项操作可能删除、覆盖或改变本机内容。确认后图灵才会继续。", smallFormat, secondaryBrush,
            left + Px(state, 18.0f), y + Px(state, 42.0f), cardW - Px(state, 36.0f), Px(state, 36.0f));
        const int buttonW = Px(state, 92), buttonH = Px(state, 34);
        state.confirmRect = {static_cast<LONG>(left + cardW - buttonW - Px(state, 16)), static_cast<LONG>(y + cardH - buttonH - Px(state, 14)), static_cast<LONG>(left + cardW - Px(state, 16)), static_cast<LONG>(y + cardH - Px(state, 14))};
        state.cancelRect = {state.confirmRect.left - buttonW - Px(state, 10), state.confirmRect.top, state.confirmRect.left - Px(state, 10), state.confirmRect.bottom};
        auto cancelFill = ConversationBrush(target, 0xEEF1F5, 0.96f);
        auto confirmFill = ConversationBrush(target, 0xED684E, 0.96f);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(state.cancelRect.left, state.cancelRect.top, state.cancelRect.right, state.cancelRect.bottom), Px(state, 10.0f), Px(state, 10.0f)), cancelFill.Get());
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(state.confirmRect.left, state.confirmRect.top, state.confirmRect.right, state.confirmRect.bottom), Px(state, 10.0f), Px(state, 10.0f)), confirmFill.Get());
        DrawConversationText(target, L"取消", smallFormat, textBrush, static_cast<float>(state.cancelRect.left + Px(state, 28)), static_cast<float>(state.cancelRect.top + Px(state, 8)), static_cast<float>(buttonW - Px(state, 20)), static_cast<float>(buttonH));
        auto white = ConversationBrush(target, 0xFFFFFF, 1.0f);
        DrawConversationText(target, L"确认执行", smallFormat, white.Get(), static_cast<float>(state.confirmRect.left + Px(state, 18)), static_cast<float>(state.confirmRect.top + Px(state, 8)), static_cast<float>(buttonW - Px(state, 20)), static_cast<float>(buttonH));
    }

    target->PopAxisAlignedClip();

    if (maxScroll > 0) {
        const float trackTop = viewport.top + Px(state, 8.0f);
        const float trackBottom = viewport.bottom - Px(state, 8.0f);
        const float trackH = trackBottom - trackTop;
        const float thumbH = std::max(static_cast<float>(Px(state, 32)), trackH * viewportH / std::max(1, state.contentHeight));
        const float travel = std::max(1.0f, trackH - thumbH);
        const float thumbTop = trackTop + travel * state.scrollOffset / maxScroll;
        auto thumb = ConversationBrush(target, 0x9DA9BB, 0.62f);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(viewport.right - Px(state, 4.0f), thumbTop, viewport.right - Px(state, 1.0f), thumbTop + thumbH), 2.0f, 2.0f), thumb.Get());
    }
    return maxScroll;
}

bool PresentConversationLayered(HWND hwnd, UINT width, UINT height) {
    auto& surface = gConversationLayered;
    RECT wr{};
    if (!GetWindowRect(hwnd, &wr)) return false;
    POINT dst{wr.left, wr.top};
    POINT src{0, 0};
    SIZE size{static_cast<LONG>(width), static_cast<LONG>(height)};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    return UpdateLayeredWindow(hwnd, nullptr, &dst, &size, surface.dc, &src, 0, &blend, ULW_ALPHA) != FALSE;
}

bool RenderConversationLayered(ConversationState& state) {
    if (!state.window) return false;
    RECT client{};
    if (!GetClientRect(state.window, &client)) return false;
    const UINT width = static_cast<UINT>(std::max<LONG>(0, client.right));
    const UINT height = static_cast<UINT>(std::max<LONG>(0, client.bottom));
    if (!EnsureConversationLayeredSurface(width, height)) return false;

    auto& surface = gConversationLayered;
    std::memset(surface.bits, 0, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    RECT bind{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (FAILED(surface.target->BindDC(surface.dc, &bind))) return false;

    auto* target = surface.target.Get();
    target->BeginDraw();
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    target->Clear(D2D1::ColorF(0, 0.0f));

    const float w = static_cast<float>(width), h = static_cast<float>(height);
    auto panelFill = ConversationBrush(target, 0xEEF4FC, 0.86f);
    auto headerFill = ConversationBrush(target, 0xF8FAFD, 0.66f);
    auto outline = ConversationBrush(target, 0xFFFFFF, 0.78f);
    auto divider = ConversationBrush(target, 0xB9C7DA, 0.34f);
    auto text = ConversationBrush(target, 0x29303D, 0.98f);
    auto secondary = ConversationBrush(target, 0x69778D, 0.94f);
    auto accent = ConversationBrush(target, 0x5B8BF4, 0.98f);
    auto inputFill = ConversationBrush(target, 0xFFFFFF, 0.80f);
    auto inputOutline = ConversationBrush(target, 0xD5DFEC, 0.84f);

    const float radius = static_cast<float>(Px(state, 24));
    const auto panel = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), radius, radius);
    target->FillRoundedRectangle(panel, panelFill.Get());
    target->DrawRoundedRectangle(panel, outline.Get(), 1.0f);

    const float headerH = static_cast<float>(Px(state, 70));
    target->PushAxisAlignedClip(D2D1::RectF(0.5f, 0.5f, w - 0.5f, headerH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target->FillRoundedRectangle(panel, headerFill.Get());
    target->PopAxisAlignedClip();
    target->DrawLine(D2D1::Point2F(0.0f, headerH - 0.5f), D2D1::Point2F(w, headerH - 0.5f), divider.Get(), 1.0f);

    const float titleSize = static_cast<float>(Px(state, 20));
    const float bodySize = static_cast<float>(Px(state, 15));
    const float smallSize = static_cast<float>(Px(state, 12));
    auto titleFormat = ConversationFormat(titleSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto bodyFormat = ConversationFormat(bodySize);
    auto bodyBold = ConversationFormat(bodySize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    auto smallFormat = ConversationFormat(smallSize);
    auto symbolFormat = ConversationFormat(static_cast<float>(Px(state, 17)), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);

    DrawConversationAvatar(target, Px(state, 34.0f), Px(state, 34.0f), Px(state, 17.0f), false);
    DrawConversationText(target, L"图灵 AI", titleFormat.Get(), text.Get(), Px(state, 60.0f), Px(state, 14.0f), Px(state, 180.0f), Px(state, 28.0f));
    auto online = ConversationBrush(target, 0x53BE68, 0.98f);
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(Px(state, 63.0f), Px(state, 50.0f)), Px(state, 4.0f), Px(state, 4.0f)), online.Get());
    DrawConversationText(target, state.busy ? L"正在工作" : L"在线", smallFormat.Get(), secondary.Get(), Px(state, 74.0f), Px(state, 40.0f), Px(state, 120.0f), Px(state, 20.0f));

    const int iconW = Px(state, 34), top = Px(state, 17);
    state.closeRect = {static_cast<LONG>(width) - Px(state, 48), top, static_cast<LONG>(width) - Px(state, 14), top + iconW};
    state.moreRect = {state.closeRect.left - Px(state, 42), top, state.closeRect.left - Px(state, 8), top + iconW};
    state.historyRect = {state.moreRect.left - Px(state, 42), top, state.moreRect.left - Px(state, 8), top + iconW};
    state.pinRect = {state.historyRect.left - Px(state, 42), top, state.historyRect.left - Px(state, 8), top + iconW};
    DrawConversationHeaderIcon(target, symbolFormat.Get(), secondary.Get(), state.pinRect, L"⌖", state.pinned);
    DrawConversationHeaderIcon(target, symbolFormat.Get(), secondary.Get(), state.historyRect, L"↶");
    DrawConversationHeaderIcon(target, symbolFormat.Get(), secondary.Get(), state.moreRect, L"•••");
    DrawConversationHeaderIcon(target, symbolFormat.Get(), secondary.Get(), state.closeRect, L"×");

    const int inputContainerH = Px(state, 64);
    const int inputTop = static_cast<int>(height) - inputContainerH - Px(state, 16);
    state.viewport = {Px(state, 22), static_cast<LONG>(headerH) + Px(state, 6), static_cast<LONG>(width) - Px(state, 22), inputTop - Px(state, 10)};
    DrawConversationTimeline(target, state, state.viewport, bodyFormat.Get(), bodyBold.Get(), smallFormat.Get(), text.Get(), secondary.Get());

    const auto inputBox = D2D1::RoundedRect(D2D1::RectF(
        Px(state, 22.0f), static_cast<float>(inputTop), w - Px(state, 22.0f), static_cast<float>(inputTop + inputContainerH)),
        Px(state, 20.0f), Px(state, 20.0f));
    target->FillRoundedRectangle(inputBox, inputFill.Get());
    target->DrawRoundedRectangle(inputBox, inputOutline.Get(), 0.9f);

    const int sendSize = Px(state, 42);
    state.sendRect = {static_cast<LONG>(width) - Px(state, 22) - sendSize - Px(state, 10), inputTop + Px(state, 11), static_cast<LONG>(width) - Px(state, 32), inputTop + Px(state, 11) + sendSize};
    const auto send = D2D1::RoundedRect(D2D1::RectF(state.sendRect.left, state.sendRect.top, state.sendRect.right, state.sendRect.bottom), sendSize * 0.5f, sendSize * 0.5f);
    auto sendFill = ConversationBrush(target, state.busy ? 0xC7D2E6 : 0x5B8BF4, 0.98f);
    target->FillRoundedRectangle(send, sendFill.Get());
    if (state.busy) {
        auto white = ConversationBrush(target, 0xFFFFFF, 1.0f);
        target->FillRectangle(D2D1::RectF(state.sendRect.left + Px(state, 16.0f), state.sendRect.top + Px(state, 16.0f), state.sendRect.right - Px(state, 16.0f), state.sendRect.bottom - Px(state, 16.0f)), white.Get());
    } else {
        auto white = ConversationBrush(target, 0xFFFFFF, 1.0f);
        const float cx = (state.sendRect.left + state.sendRect.right) * 0.5f;
        const float cy = (state.sendRect.top + state.sendRect.bottom) * 0.5f;
        target->DrawLine(D2D1::Point2F(cx, cy + Px(state, 7.0f)), D2D1::Point2F(cx, cy - Px(state, 7.0f)), white.Get(), 1.8f);
        target->DrawLine(D2D1::Point2F(cx, cy - Px(state, 7.0f)), D2D1::Point2F(cx - Px(state, 5.0f), cy - Px(state, 2.0f)), white.Get(), 1.8f);
        target->DrawLine(D2D1::Point2F(cx, cy - Px(state, 7.0f)), D2D1::Point2F(cx + Px(state, 5.0f), cy - Px(state, 2.0f)), white.Get(), 1.8f);
    }

    const int actionSize = Px(state, 34), gap = Px(state, 4);
    const int micLeft = state.sendRect.left - Px(state, 8) - actionSize;
    const int attachLeft = micLeft - gap - actionSize;
    const int actionTop = inputTop + Px(state, 15);
    surface.attachRect = {attachLeft, actionTop, attachLeft + actionSize, actionTop + actionSize};
    surface.micRect = {micLeft, actionTop, micLeft + actionSize, actionTop + actionSize};
    DrawConversationPaperclip(target, secondary.Get(), attachLeft + actionSize * 0.5f, actionTop + actionSize * 0.5f);
    DrawConversationMic(target, secondary.Get(), micLeft + actionSize * 0.5f, actionTop + actionSize * 0.5f - Px(state, 1.0f));

    const std::wstring inputText = ReadText(state.input);
    const float inputLeft = static_cast<float>(Px(state, 38));
    const float inputRight = static_cast<float>(attachLeft - Px(state, 8));
    if (inputText.empty()) DrawConversationText(target, L"输入消息…", bodyFormat.Get(), secondary.Get(), inputLeft, inputTop + Px(state, 20.0f), inputRight - inputLeft, Px(state, 28.0f));
    else DrawConversationText(target, inputText, bodyFormat.Get(), text.Get(), inputLeft, inputTop + Px(state, 17.0f), inputRight - inputLeft, Px(state, 34.0f));

    DrawConversationText(target, L"Enter 发送 · Shift + Enter 换行 · Esc 停止 / 收起", smallFormat.Get(), secondary.Get(),
        Px(state, 130.0f), h - Px(state, 15.0f), w - Px(state, 260.0f), Px(state, 15.0f));

    const HRESULT result = target->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) {
        ReleaseConversationLayeredSurface();
        return false;
    }
    if (FAILED(result)) return false;
    return PresentConversationLayered(state.window, width, height);
}

bool ConversationPointInsidePanel(HWND hwnd, POINT screenPoint, int radius) {
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

void PositionConversationInputProxy(ConversationState& state) {
    if (!state.input) return;
    // Native EDIT owns IME/clipboard/voice typing only. Direct2D owns every visible pixel.
    MoveWindow(state.input, Px(state, 38), Px(state, 1), 1, 1, FALSE);
}

} // namespace
} // namespace turingdesk
