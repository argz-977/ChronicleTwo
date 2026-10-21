#include "font.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <vector>

#include "font_port.hpp"
#include "log/log.hpp"
#include "mainloop.hpp"
#include "mg_drawprim_port.hpp"
#include "mglib_port.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "platform/ttffont.hpp"

char32_t FontPortCharacter(int language, int glyph, std::span<const std::uint8_t> table) {
    if (language == LANG_JAPANESE || glyph < 0 || glyph >= FONT_NO_ALPHABETICAL_TOP ||
        static_cast<std::size_t>(glyph) >= table.size() / 2) {
        return 0;
    }
    const auto first = table[static_cast<std::size_t>(glyph) * 2];
    const auto second = table[static_cast<std::size_t>(glyph) * 2 + 1];
    return first >= 0x20 && first <= 0x7e && second == 0x20 ? first : 0;
}

namespace {

bool Ready() {
    static bool tried = false;
    static bool loaded = false;
    if (!tried) {
        tried = true;
        const std::array roots = {PathsSaveRoot(), PathsExecutable().parent_path()};
        for (const auto &root : roots) {
            const auto      file = root / "lang" / "font.ttf";
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error)) {
                continue;
            }
            // This opt-in path accepts a trusted user font. The shared rasterizer is not a font validator.
            const auto bytes = std::filesystem::file_size(file, error);
            if (!error && bytes >= 12 && bytes <= 32 * 1024 * 1024) {
                loaded = ttffont::Load(file);
            }
            if (loaded) {
                CT_LOG_INFO(Gfx, "font: sharp ASCII text from {}", PathsDisplay(file));
            }
            break; // A present save override wins, including when it cannot be loaded.
        }
        if (!loaded) {
            CT_LOG_INFO(Gfx, "font: no usable lang/font.ttf; keeping original text (docs/PC.md)");
        }
    }
    return loaded;
}

struct Ink {
    gfx::TextureHandle texture = gfx::kNullTexture;
    int                width = 0, height = 0, x = 0, y = 0;
};

// Immutable images avoid overwriting any glyph a recorded or interpolated frame still references.
// RendererShutdown releases them. No runtime font reload or eviction; allocation failure keeps retail.
const Ink *GetInk(char32_t ch, int pixels) {
    static std::map<std::pair<char32_t, int>, Ink> cache;
    static std::size_t                             bytes_used = 0;
    constexpr std::size_t budget = 16 * 1024 * 1024;
    const auto            key = std::make_pair(ch, pixels);
    if (const auto found = cache.find(key); found != cache.end()) {
        return found->second.texture == gfx::kNullTexture ? nullptr : &found->second;
    }
    if (cache.size() >= 2048) {
        return nullptr;
    }
    Ink                        ink;
    std::vector<unsigned char> coverage;
    if (!ttffont::RenderGlyph(ch, pixels, coverage, ink.width, ink.height, ink.x, ink.y) ||
        ink.width <= 0 || ink.height <= 0 || ink.width > 512 || ink.height > 512) {
        cache.emplace(key, ink);
        return nullptr;
    }
    const auto bytes = static_cast<std::size_t>(ink.width + 2) * (ink.height + 2) * 4;
    if (bytes > budget - bytes_used) {
        cache.emplace(key, ink);
        return nullptr;
    }
    std::vector<unsigned char> rgba(bytes, 0);
    for (int row = 0; row < ink.height; ++row) {
        for (int col = 0; col < ink.width; ++col) {
            const auto target = (static_cast<std::size_t>(row + 1) * (ink.width + 2) + col + 1) * 4;
            rgba[target] = rgba[target + 1] = rgba[target + 2] = 255;
            rgba[target + 3] = coverage[static_cast<std::size_t>(row) * ink.width + col];
        }
    }
    gfx::TextureDesc desc;
    desc.width = static_cast<std::uint32_t>(ink.width + 2);
    desc.height = static_cast<std::uint32_t>(ink.height + 2);
    ink.texture = gfx::CreateTexture(desc);
    if (ink.texture != gfx::kNullTexture) {
        gfx::UpdateTextureNow(ink.texture, 0, 0, 0, desc.width, desc.height, rgba.data());
        bytes_used += bytes;
    }
    const auto [entry, inserted] = cache.emplace(key, ink);
    return entry->second.texture == gfx::kNullTexture ? nullptr : &entry->second;
}

bool DrawSharp(const CFont &font, mgCDrawPrim &prim, int glyph, int x, int y, int outline,
               RGBAQ_TYPE color, unsigned char alpha) {
    if (!ConfigGet().font_sharp || font.mini != 0 || font.draw_w <= 0 || font.draw_h <= 0) {
        return false;
    }
    const int      count = std::clamp(GetYoyakuTblNum(), 0, 0x7fc);
    const char32_t ch = FontPortCharacter(LanguageCode, glyph, {GetYoyakuTblTop(), static_cast<std::size_t>(count) * 2});
    if (ch == 0 || !Ready()) {
        return false;
    }
    if (ch == 0x20) {
        return true;
    }
    const auto  cell_w = static_cast<float>(font.draw_w / 2);
    const auto  cell_h = static_cast<float>(font.draw_h);
    const auto  metrics = ttffont::GetMetrics();
    const float line = metrics.ascent + metrics.descent;
    if (!(line > 0.0f) || !(metrics.advance > 0.0f) || cell_w <= 0.0f) {
        return false;
    }
    const float em = std::min(cell_h * 0.9f / line, cell_w / metrics.advance);
    const auto  mapping = gfx::GetUiMapping(mgPortCurrentTarget());
    const float scale = std::max(mapping.scale_x, mapping.scale_y);
    if (!(em > 0.0f) || !std::isfinite(em) || !std::isfinite(scale) || !(scale > 0.0f)) {
        return false;
    }
    // Round up to a small bucket: at least one raster pixel per output pixel, few resize cache entries.
    const int pixels = static_cast<int>(std::ceil(std::min(em * scale, 512.0f) / 8.0f)) * 8;
    if (pixels < 8 || em * scale > 512.0f) {
        return false;
    }
    const Ink *ink = GetInk(ch, pixels);
    if (!ink) {
        return false;
    }
    const float unit = em / pixels;
    const float ink_w = ink->width * unit;
    const float width = std::min(cell_w, ink_w);
    const float height = ink->height * unit;
    // One common baseline; fit ink into the cell without using TTF advances to move the game pen.
    const float                left = x + (cell_w - width) * 0.5f;
    const float                top = y + (cell_h - em * line) * 0.5f + em * metrics.ascent + ink->y * unit;
    std::vector<gfx::Vertex2D> quads;
    quads.reserve(52);
    auto quad = [&](float dx, float dy, int r, int g, int b, int a) {
        const std::uint8_t tint[] = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                     static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
        auto               vertex = [&](float px, float py, float u, float v) {
            quads.push_back(mgPortPrimVertex(prim, px + dx, py + dy, 0, u, v, tint));
        };
        vertex(left, top, 1, 1);
        vertex(left + width, top, 1 + ink->width, 1);
        vertex(left + width, top + height, 1 + ink->width, 1 + ink->height);
        vertex(left, top + height, 1, 1 + ink->height);
    };
    auto cross = [&](int distance, int opacity) {
        quad(0, -distance, 0, 0, 0, opacity);
        quad(0, distance, 0, 0, 0, opacity);
        quad(distance, 0, 0, 0, 0, opacity);
        quad(-distance, 0, 0, 0, 0, opacity);
    };
    if (outline != 0) {
        switch (font.fuchi) {
            case FUCHI_SHADOW_WHITE:
                quad(1, 1, 255, 255, 255, alpha * 64 / 128);
                break;
            case FUCHI_SHADOW_BLACK:
                quad(1, 1, 0, 0, 0, alpha * 64 / 128);
                break;
            case FUCHI_OUTLINE:
                cross(1, alpha);
                break;
            case FUCHI_SHADOW_DOUBLE:
                quad(1, 1, 64, 64, 64, alpha);
                quad(2, 2, 0, 0, 0, alpha);
                break;
            case FUCHI_SHADOW_BLACK_WIDE:
                quad(2, 2, 0, 0, 0, alpha);
                break;
            case FUCHI_OUTLINE_WIDE:
                cross(2, alpha * 32 / 128);
                break;
            case FUCHI_SHADOW_WHITE_BLUE:
                quad(1, 1, 255, 255, 255, alpha * 255 / 128);
                quad(-1, -1, 42, 43, 49, alpha);
                quad(-2, -2, 42, 43, 49, alpha);
                break;
            case FUCHI_OUTLINE_THICK:
                cross(1, alpha);
                quad(-1, -1, 0, 0, 0, alpha);
                quad(-1, 1, 0, 0, 0, alpha);
                quad(1, -1, 0, 0, 0, alpha);
                quad(1, 1, 0, 0, 0, alpha);
                cross(2, alpha);
                break;
        }
    }
    quad(0, 0, color.r, color.g, color.b, alpha * color.a / 128);
    gfx::TextureBinding binding;
    binding.texture = ink->texture;
    mgPortPrimSubmitTexture(prim, quads, binding);
    return true;
}

} // namespace

PORT_OVERRIDE void CFont::DrawChar(mgCDrawPrim *prim, int font_no, int x, int y, int outline,
                                   RGBAQ_TYPE glyph_color, u8 alpha) {
    if (font_no < 0) {
        return;
    }
    if (DrawSharp(*this, *prim, font_no, x, y, outline, glyph_color, alpha)) {
        return;
    }
    int  page = 0;
    RECT source = mini ? GetRectFontTexMini(font_no, &page) : GetRectFontTex(font_no, &page);
    if (mini) {
        MySetTexMini(page, prim);
    } else {
        MySetTex(page, prim);
    }
    RECT destination{.x = x, .y = y, .width = draw_w, .height = draw_h};
    if (CheckHalfFont(font_no)) {
        destination.width /= 2;
        source.width /= 2;
    }
    if (outline) {
        set2DSprite_Fuchi(prim, destination, source, fuchi, alpha);
    }
    glyph_color.a = alpha * glyph_color.a / 128;
    set2DSpriteEasyFont(prim, {destination.x, destination.y, destination.width, destination.height},
                        {source.x, source.y, source.width, source.height}, &glyph_color);
}
