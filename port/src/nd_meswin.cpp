#include "common.h"

#include <cstring>
#include <utility>

#include "drawwin.hpp"
#include "menu_mouse.hpp"
#include "mg_drawprim.hpp"
#include "mglib_port.hpp"
#include "nd_meswin.hpp"

namespace {

/**
 * Records the selected mouse choice for the next Yes/No label draw.
 */
int yesno_highlight = -1;

/**
 *
 * Draws one textured part of a versatile message frame.
 *
 */
void DrawWindowPart(mgCDrawPrim *prim, VersatileWinPart part, int x, int y, int width, int height,
                    RGBAQ_TYPE *color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    mgRect<int> screen;
    mgRect<int> texture;
    screen.Set(x, y, width, height);
    texture.Set(data[part][0], data[part][1], data[part][2], data[part][3]);
    set2DSprite(prim, screen, texture, color);
}

} // namespace

PORT_OVERRIDE void ClsMes::SetSelectCursorPos(RECT rect) {
    CalcSelectCursorPos(rect, &choice_pos[0][0]);
    yesno_highlight = window_mode == MES_WIN_YESNO && select >= 0 && select < 2 &&
                              fade >= 1 && alpha > 0 && buff && font_h > 0 && MenuMousePointerVisible()
                          ? select
                          : -1;
}

PORT_OVERRIDE void DrawYesNo(mgCDrawPrim *primitive, int yes_x, int yes_y, int no_x, int no_y, RGBAQ_TYPE *color) {
    const int selected = std::exchange(yesno_highlight, -1);
    for (int choice = 0; choice < 2; ++choice) {
        RGBAQ_TYPE tint = *color;
        // Brighten the mouse choice once, as retail costume selection does.
        if (choice == selected) {
            tint.r = tint.g = tint.b = 0xA4;
        }
        set2DSprite(primitive, {choice == 0 ? yes_x : no_x, choice == 0 ? yes_y : no_y, 60, 26},
                    {choice == 0 ? 136 : 196, 230, 60, 26}, &tint);
    }
}

// The gaiji frame exposes the backing at the top and left edges of its opening. Move those edges outward
// without shortening the right and bottom edges of the original rectangle.
PORT_OVERRIDE void FillRect(int x, int y, int w, int h, int r, int g, int b, int a) {
    mgCDrawPrim drawer;
    drawer.Initialize(nullptr, nullptr);
    drawer.AlphaBlendEnable(1);
    drawer.AlphaBlend(MG_ALPHA_BLEND_NORMAL);
    drawer.AlphaTestEnable(1);
    drawer.AlphaTest(1, 0);
    drawer.DepthTestEnable(0);
    drawer.ZMask(MG_Z_MASK_MASKED);
    drawer.Bilinear(0);
    drawer.TextureMapEnable(0);
    drawer.Begin(MG_PRIM_SPRITE);
    drawer.Color(r, g, b, a);
    drawer.Vertex(x - 1, y - 1, 0);
    drawer.Vertex(x + w, y + h, 0);
    drawer.End();
}

PORT_OVERRIDE void DrawVersatileWin_3(mgCDrawPrim *prim, RECT win, int select_y, RGBAQ_TYPE *color, int alpha,
                                      int opaque) {
    MySetPrim(prim, MES_PRIM_SPRITE, 0);
    const int band_top = select_y - 7;
    const int band_bottom = select_y + 7;
    const int inside_x = win.x + 0x17;
    const int right_x = win.x + win.width - 0x17;
    const int inside_width = win.width - 0x2E;
    const int side_y = win.y + 0x19;
    const int bottom_y = win.y + win.height - 0x19;
    const int side_height = band_top - side_y;
    const int lower_height = bottom_y - band_bottom;

    DrawWindowPart(prim, VWIN_TOP_L, win.x, win.y, 0x17, 0x19, color);
    DrawWindowPart(prim, VWIN_TOP_C, inside_x, win.y, inside_width, 0x19, color);
    DrawWindowPart(prim, VWIN_TOP_R, right_x, win.y, 0x17, 0x19, color);
    DrawWindowPart(prim, VWIN_SIDE_L, win.x, side_y, 0x17, side_height, color);
    if (side_height + 0xD > 0 && inside_width + 0x16 > 0) {
        const int fill_alpha = opaque != 0 ? 0x80 : alpha * 0x36 / 128;
        FillRect(inside_x - 0xA, side_y - 9, inside_width + 0x16, side_height + 0xD, 0, 0, 0, fill_alpha);
    }
    DrawWindowPart(prim, VWIN_SIDE_R, right_x, side_y, 0x17, side_height, color);
    DrawWindowPart(prim, VWIN_BAND_L, win.x, band_top, 0x17, 0xE, color);
    DrawWindowPart(prim, VWIN_BAND_C, inside_x, band_top, inside_width, 0xE, color);
    DrawWindowPart(prim, VWIN_BAND_R, right_x, band_top, 0x17, 0xE, color);
    DrawWindowPart(prim, VWIN_LOWER_SIDE_L, win.x, band_bottom, 0x17, lower_height, color);
    DrawWindowPart(prim, VWIN_LOWER_SIDE_C, inside_x, band_bottom, inside_width, lower_height, color);
    DrawWindowPart(prim, VWIN_LOWER_SIDE_R, right_x, band_bottom, 0x17, lower_height, color);
    DrawWindowPart(prim, VWIN_LOWER_BOTTOM_L, win.x, bottom_y, 0x17, 0x19, color);
    DrawWindowPart(prim, VWIN_LOWER_BOTTOM_C, inside_x, bottom_y, inside_width, 0x19, color);
    DrawWindowPart(prim, VWIN_LOWER_BOTTOM_R, right_x, bottom_y, 0x17, 0x19, color);
}

PORT_OVERRIDE void ClsMes::StepSelectCursor(int steps) {
    if (select < 0) {
        cursor_time = 0;
        return;
    }
    SetGoalCursorXY();
    if (cursor_time <= 0) {
        cursor_x = goal_cursor_x;
        cursor_y = goal_cursor_y;
        cursor_time = 1;
    } else {
        for (int i = 0; i < steps; ++i) {
            cursor_x = (cursor_x + goal_cursor_x) / 2;
            cursor_y = (cursor_y + goal_cursor_y) / 2;
            ++cursor_time;
        }
    }
    if (fade < 1 || alpha <= 0 || !buff || font_h <= 0) {
        return;
    }
    const auto choices = MenuMouseMessageRows(*this, (text_h + font_h - 1) / font_h);
    if (std::cmp_less(select, choices.size())) {
        MenuMouseNoteChoices(this, mes_no, choices, select, window_mode == MES_WIN_YESNO);
    }
}

PORT_OVERRIDE void _set2DSprite(char *texture_name, mgCDrawPrim *primitive, mgRect<int> destination,
                                mgRect<int> texture, RGBAQ_TYPE *color) {
    if (MesAbsDrawOff != 0) {
        return;
    }
    const bool         cursor = std::strcmp(texture_name, "gaiji") == 0 && texture.left == 96 && texture.top == 232 &&
                                texture.right == 40 && texture.bottom == 24;
    gfx::Interp2DScope tag(cursor ? gfx::Interp2D{.key = MenuMouseNextHandKey(), .generation = static_cast<uint64_t>(mgPortFrameCount())} : gfx::CurrentInterp2D());
    primitive->Begin(MG_PRIM_SPRITE);
    MySetTex(texture_name, primitive);
    if (cursor && (color->r || color->g || color->b)) {
        MenuMouseNoteHand(mgPortBinding(primitive->texture.tex0_bits, primitive->texture.tex1_bits),
                          {.x = static_cast<float>(texture.left), .y = static_cast<float>(texture.top), .w = static_cast<float>(texture.right), .h = static_cast<float>(texture.bottom)},
                          0, color->a);
    }
    set2DSpriteEasy(primitive, destination, texture, color);
    primitive->End();
}
