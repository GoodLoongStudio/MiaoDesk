#pragma once

#include <filesystem>
#include <string>

struct ID2D1RenderTarget;
struct ID2D1Bitmap;

namespace miaodesk::content {

// Decodes a packaged image into an ID2D1Bitmap bound to one render target.
//
// This is the 2D counterpart to MiaoD3D11TextureLoader: the same package asset,
// the same WIC front end, a different destination resource. They are deliberately
// separate files rather than one loader with a backend switch, because the size
// policy genuinely differs (see the .cpp) and because a shared header would drag
// both d2d1.h and d3d11.h into every caller.
class MiaoD2DTextureLoader {
public:
    // The W suffix is mandatory, not decoration — the same rule the D3D11 loader's
    // header documents. <windows.h> turns the plain name LoadImage into an A/W macro,
    // so calling it LoadImage here makes the preprocessor rename the qualified member
    // to LoadImageW in *this* translation unit and not in the declaration. The first
    // draft of this header carried a comment claiming the suffix was unnecessary here;
    // the cross-compile gate corrected it, which is the whole reason the gate exists.
    static bool LoadImageW(
        ID2D1RenderTarget* target,
        const std::filesystem::path& path,
        ID2D1Bitmap** bitmap,
        std::wstring* error = nullptr);

    static bool SelfTestPathPolicy();
};

} // namespace miaodesk::content
