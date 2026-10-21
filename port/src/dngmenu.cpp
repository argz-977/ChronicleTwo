#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <vector>

#include "dngmenu_port.hpp"
#include "floor_monsters.hpp"
#include "mainloop.hpp"
#include "memcard.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "menuop.hpp"
#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "stubs/dngmenu.hpp"

namespace {

// How a passage cell is drawn: which edges of the cell its line joins.
enum class RootShape : u8 {
    Vertical = 0,     // top centre down to the bottom centre
    Horizontal = 1,   // left to right across the middle
    RightAndDown = 2, // middle to the right, branching down from the middle
    LeftAndDown = 3,  // left to the middle, branching down from the middle
    UpAndRight = 4,   // top centre down to the middle, then to the right
    UpAndLeft = 5,    // left to the middle, then up to the top centre
    DownToRight = 6,  // bottom centre up to the right of the middle
    LeftToDown = 7,   // left of the middle down to the bottom centre
    UpToRight = 8,    // top centre down to the right of the middle
    LeftToUp = 9,     // left of the middle up to the top centre
};

// Offset of a passage mark from its grid cell.
struct RootMarkOffset {
    s16 x;
    s16 y;
};

constexpr RootMarkOffset kRootMarkOffsets[10] = {
    {.x = 6, .y = -2},
    {.x = 4, .y = -2},
    {.x = 4, .y = -2},
    {.x = 4, .y = -2},
    {.x = 4, .y = -2},
    {.x = 4, .y = -2},
    {.x = 4, .y = -2},
    {.x = 4, .y = -2},
    {.x = 4, .y = -2},
    {.x = 4, .y = -2},
};

// The one passage mark of dungeon six that sits off its usual place.
constexpr RootMarkOffset kZerumaitoMarkOffset = {.x = 13, .y = -11};

// Texture coordinates of the passage-type marks.
constexpr s16 kRootTypeTexture[5][2] = {
    {0,   0 },
    {490, 0 },
    {490, 22},
    {490, 44},
    {294, 18},
};

// Texture rectangles of the visited-room labels; a negative left ends the table.
constexpr s16 kRoomLabelTexture[4][4] = {
    {0,  172, 62, 22},
    {0,  194, 62, 20},
    {0,  216, 62, 20},
    {-1, 0,   0,  0 },
};

// Destination offsets of the visited-room labels within the room picture.
constexpr RootMarkOffset kRoomLabelOffset[4] = {
    {.x = 20, .y = -7},
    {.x = 20, .y = -7},
    {.x = 20, .y = 0 },
    {.x = 10, .y = 10},
};

// Room-mark animation speed in menu and event modes.
constexpr float kRoomMarkStep[2] = {0.0628318563f, 0.125663713f};

// Retail strokes a passage with three lines a pixel apart, some of them not parallel. The GS draws a line one pixel
// thick across its minor axis, so the three join into one solid stroke; a host line is one physical pixel wide and
// leaves three hairlines at native resolution. Each three lines are filled here as one band: the convex hull of the
// pixel-wide bands the GS covers.
class PassageStroke {
public:
    explicit PassageStroke(mgCDrawPrim &builder) : prim(builder) {}

    void Color() {
        Flush();
        mgPortPrimColor(prim, color);
    }

    void Line(float x0, float y0, float x1, float y1) {
        const bool  x_major = std::fabs(x1 - x0) >= std::fabs(y1 - y0);
        const float across_x = x_major ? 0.0f : 1.0f;
        const float across_y = x_major ? 1.0f : 0.0f;
        points.push_back({.x = x0, .y = y0});
        points.push_back({.x = x1, .y = y1});
        points.push_back({.x = x1 + across_x, .y = y1 + across_y});
        points.push_back({.x = x0 + across_x, .y = y0 + across_y});

        if (points.size() == kBandPoints) {
            FillBand();
        }
    }

    void Flush() {
        FillBand();

        if (!vertices.empty()) {
            mgPortPrimSubmit(prim, gfx::Primitive::Triangles, vertices);
            vertices.clear();
        }
    }

private:
    struct Point {
        float x;
        float y;
    };

    // Four corners for each of a passage's three lines.
    static constexpr std::size_t kBandPoints = 12;

    // Fills the convex hull of the pending corners as a triangle fan.
    void FillBand() {
        if (points.empty()) {
            return;
        }

        std::ranges::sort(points, [](const Point &a, const Point &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
        const auto cross = [](const Point &o, const Point &a, const Point &b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        // Andrew's monotone chain: the lower hull, then the upper, without collinear points.
        std::vector<Point> hull;

        for (int pass = 0; pass < 2; ++pass) {
            const std::size_t first = hull.size();

            for (std::size_t n = 0; n < points.size(); ++n) {
                const Point &point = pass == 0 ? points[n] : points[points.size() - 1 - n];

                while (hull.size() >= first + 2 && cross(hull[hull.size() - 2], hull.back(), point) <= 0.0f) {
                    hull.pop_back();
                }

                hull.push_back(point);
            }

            hull.pop_back();
        }

        points.clear();
        const auto vertex = [this](const Point &point) {
            return mgPortPrimVertex(prim, point.x, point.y, 0.0f, 0.0f, 0.0f, color);
        };

        for (std::size_t i = 1; i + 1 < hull.size(); ++i) {
            vertices.insert(vertices.end(), {vertex(hull[0]), vertex(hull[i]), vertex(hull[i + 1])});
        }
    }

    mgCDrawPrim               &prim;
    uint8_t                    color[4] = {};
    std::vector<Point>         points;
    std::vector<gfx::Vertex2D> vertices;
};

} // namespace

// Retail skips passages and rooms past the right and bottom of the 4:3 screen, which a wider window shows.
// Both are culled against the visible extent in UI units instead, and the passage lines are filled as solid strokes.
PORT_OVERRIDE void CDngFreeMap::DrawRoot(mgRect<float> rect, DNGMAP_ROOT_INFO *root, int shadow, unsigned int marks,
                                         int opacity) {
    const auto extent = mgPortVisibleUiExtent(mgPortCurrentTarget());

    if (root == nullptr || extent.right < rect.left || rect.top > extent.bottom + 20.0f) {
        return;
    }

    mgRect<float> &put = treemap_root_put;
    put = rect;

    if (shadow) {
        put.left += 8.0f;
        put.top += 8.0f;
    }

    float                 mark_color = 128.0f;
    float                 red = 212.0f;
    float                 green = 192.0f;
    float                 blue = 144.0f;
    const RootMarkOffset *mark = kRootMarkOffsets;

    if (mode == DNGMAP_MODE_EVENT) {
        mark_color = 64.0f;
        red = 128.0f;
        green = 111.0f;
        blue = 0.0f;
    }

    mgCDrawPrim *prim = GetMenuPrim();
    SetSpriteEnv(prim, MENU_SPRITE_ENV_COLOR_AA);
    prim->Begin(MG_PRIM_LINE);
    prim->Color(static_cast<int>(red), static_cast<int>(green), static_cast<int>(blue), opacity);

    if (shadow) {
        prim->Color(0, 0, 0, static_cast<int>(0.05f * static_cast<float>(opacity)));
    }

    PassageStroke stroke(*prim);
    stroke.Color();
    const auto shape = static_cast<RootShape>(root->shape);

    if (shape == RootShape::Horizontal || shape == RootShape::RightAndDown || shape == RootShape::LeftAndDown ||
        shape == RootShape::DownToRight || shape == RootShape::LeftToDown) {
        put.left -= 5.0f;
    }

    put.right = put.left + 52.0f;
    put.bottom = put.top + 20.0f;

    switch (shape) {
        case RootShape::Vertical:
            put.left += 26.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left + i, put.top, put.left + i + -16.0f, put.bottom);
            }

            mark = &kRootMarkOffsets[0];

            if (dng_no == 6) {
                mark = &kZerumaitoMarkOffset;
            }

            break;
        case RootShape::Horizontal:
            if (marks & DNGMAP_GLID_MARK_SPECIAL_LEFT) {
                put.left += 14.0f;
            }

            put.top += 10.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left - i, put.top + i, put.right - i, put.top + i);
            }

            mark = &kRootMarkOffsets[1];
            break;
        case RootShape::RightAndDown:
            put.left += 25.0f;
            put.top += 10.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left - i, put.top + i, put.right - i, put.top + i);
            }

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left + i, put.top, put.left + i - 10.0f, put.bottom);
            }

            mark = &kRootMarkOffsets[2];
            break;
        case RootShape::LeftAndDown:
            put.right -= 27.0f;
            put.top += 10.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left - i, put.top + i, put.right - i, put.top + i);
            }

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.right + i, put.top, put.right + i - 10.0f, put.bottom);
            }

            mark = &kRootMarkOffsets[3];
            break;
        case RootShape::UpAndRight: {
            put.left += 26.0f;
            put.bottom -= 10.0f;
            const float inner_left = put.left - 2.0f;
            const float inner_bottom = put.bottom + 2.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left + i, put.top, put.left + i - 10.0f, inner_bottom);
            }

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(inner_left - i - 5.0f, put.bottom + i, put.right - i - 5.0f,
                            put.bottom + i);
            }

            mark = &kRootMarkOffsets[4];
            break;
        }
        case RootShape::UpAndLeft:
            put.right -= 26.0f;
            put.bottom -= 10.0f;
            put.left += 1.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left - i - 5.0f - 1.0f, put.bottom + i, put.right - i - 5.0f - 1.0f,
                            put.bottom + i);
            }

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.right + i, put.top, put.right + i - 10.0f + 1.0f, put.bottom);
            }

            mark = &kRootMarkOffsets[5];
            put.left = put.right;
            break;
        case RootShape::DownToRight:
            put.left += 15.5f;
            put.top += 10.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left + i, put.bottom, put.right - i, put.top + i);
            }

            break;
        case RootShape::LeftToDown:
            put.left -= 1.0f;
            put.right -= 35.0f;
            put.top += 10.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left - i / 2.0f, put.top + i, put.right - i, put.bottom);
            }

            break;
        case RootShape::UpToRight:
            put.left += 26.0f;
            put.right -= 5.5f;
            put.bottom -= 10.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left + 1.5f - i, put.top, put.right - i / 2.0f, put.bottom + i);
            }

            break;
        case RootShape::LeftToUp:
            put.left -= 6.0f;
            put.right -= 26.0f;
            put.bottom -= 10.0f;

            for (float i = 0.0f; i < 3.0f; i++) {
                stroke.Line(put.left - i, put.bottom + i, put.right + i, put.top);
            }

            break;
    }

    stroke.Flush();
    prim->End();
    prim->Bilinear(0);
    prim->TextureMapEnable(1);
    prim->Begin(MG_PRIM_SPRITE);
    prim->Color(static_cast<int>(red), static_cast<int>(green), static_cast<int>(blue), opacity);

    if (shadow) {
        prim->Color(0, 0, 0, static_cast<int>(0.05f * static_cast<float>(opacity)));
    }

    prim->Texture(map_tex);

    if (root->type != DNGMAP_ROOT_NORMAL && static_cast<u8>(root->opened) != 0 && root->show_mark && mark != nullptr) {
        prim->Color(static_cast<int>(mark_color), static_cast<int>(mark_color), static_cast<int>(mark_color), opacity);

        if (shadow) {
            prim->Color(0, 0, 0, static_cast<int>(0.05f * static_cast<float>(opacity)));
        }

        const int type = root->type;
        prim->TextureCrd(kRootTypeTexture[type][0], kRootTypeTexture[type][1]);
        prim->Vertex(rect.left + static_cast<float>(mark->x), rect.top + static_cast<float>(mark->y), 0.0f);
        prim->TextureCrd(kRootTypeTexture[type][0] + 22, kRootTypeTexture[type][1] + 22);
        prim->Vertex(rect.left + static_cast<float>(mark->x) + 22.0f, rect.top + static_cast<float>(mark->y) + 22.0f, 0.0f);
    }

    prim->End();
}

std::optional<mgRect<float> > PortDngRoomPicture(int dng_no, const DNGMAP_ROOM_INFO &room, float x, float y, float right,
                                                 float bottom) {
    if (x > right + 20.0f || y > bottom + 30.0f) {
        return std::nullopt;
    }

    mgRect<float> picture(x - 30.0f, y - 42.0f, 96.0f, 66.0f);

    if (dng_no == 4 || dng_no == 5 || dng_no == 6) {
        picture.right = 100.0f;
        picture.bottom = 68.0f;
    }

    if (room.visited) {
        // Sub and boss rooms draw their larger special picture.
        if ((room.flag & DNGMAP_ROOM_FLAG_SUB) || (room.flag & DNGMAP_ROOM_FLAG_BOSS)) {
            picture.right = 96.0f;
            picture.bottom = 96.0f;
        }

        picture.left += static_cast<float>(room.offset_x);
        picture.top += static_cast<float>(room.offset_y);
    }

    return picture;
}

PORT_OVERRIDE void CDngFreeMap::DrawRoomOne(mgRect<float> rect, DNGMAP_ROOM_INFO *room, unsigned int /*unused*/,
                                            int opacity, float brightness) {
    if (room == nullptr) {
        return;
    }

    const auto extent = mgPortVisibleUiExtent(mgPortCurrentTarget());
    const auto placed = PortDngRoomPicture(dng_no, *room, rect.left, rect.top, extent.right, extent.bottom);

    if (!placed) {
        return;
    }

    const mgRect<float> picture = *placed;
    mgCDrawPrim        *prim = GetMenuPrim();
    SetSpriteEnv(prim, MENU_SPRITE_ENV_TEXTURE);
    mgRect<int> tex;
    mgRect<int> special;
    tex.Set(0, 66, 96, 66);
    special.Set(192, 198, 96, 96);
    const int tex_no = room->tex_no;

    if (!room->visited) {
        tex.left = 0;
        tex.top = 0;
    } else {
        const int column = tex_no % 5;
        const int row = tex_no / 5;
        tex.left += tex.right * column;
        tex.top += tex.bottom * row;

        if (room->flag & DNGMAP_ROOM_FLAG_START) {
            tex.left = 96;
            tex.top = 0;
        }

        if (room->flag & DNGMAP_ROOM_FLAG_EXIT) {
            tex.left = 192;
            tex.top = 0;
        }

        if ((room->flag & DNGMAP_ROOM_FLAG_SUB) || (room->flag & DNGMAP_ROOM_FLAG_BOSS)) {
            tex.left = special.left + (room->tex_no % 3) * 96;
            tex.top = special.top + (room->tex_no / 3) * 96;
            tex.right = special.right;
            tex.bottom = special.bottom;

            if (room->tex_no >= 3) {
                tex.bottom = 90;
            }
        }
    }

    int   level = static_cast<int>(128.0f * brightness);
    float shadow_alpha = 0.25f * static_cast<float>(opacity);
    float event_brightness = 1.0f;

    if (mode == DNGMAP_MODE_EVENT && user_glid != nullptr && &user_glid->room != room) {
        level = static_cast<int>(64.0f * brightness);
        event_brightness = 0.5f;
    }

    if (mode == DNGMAP_MODE_MENU) {
        prim->Begin(MG_PRIM_SPRITE);
        prim->Texture(map_tex);
        prim->Color(0, 0, 0, static_cast<int>(shadow_alpha));
        PrimQuad(prim, picture.left + 8.0f, picture.top + 8.0f, tex);
        prim->End();
    }

    SetSpriteEnv(prim, MENU_SPRITE_ENV_TEXTURE);
    room->mark_phase += kRoomMarkStep[mode];

    if (room->mark_phase > std::numbers::pi_v<float>) {
        room->mark_phase -= TWO_PI;
    }

    int r = 192;
    int g = 192;
    int b = 192;

    if (room->mark) {
        float phase = room->mark_phase;

        while (phase > std::numbers::pi_v<float>) {
            phase -= TWO_PI;
        }

        while (phase < -std::numbers::pi_v<float>) {
            phase += TWO_PI;
        }

        if (phase > 0.0f) {
            b = g = r = static_cast<int>(7.0f * static_cast<float>(level) / 8.0f);
        }
    } else {
        b = g = r = level;
    }

    prim->Bilinear(0);
    prim->Begin(MG_PRIM_SPRITE);
    prim->Texture(map_tex);
    prim->Color(r, g, b, opacity);
    PrimQuad(prim, picture, tex);
    prim->End();

    if (!room->visited && user_glid != nullptr && &user_glid->room != room) {
        const float overlay_left = picture.left + 40.0f;
        const float overlay_top = picture.top + 25.0f;
        prim->Begin(MG_PRIM_SPRITE);
        prim->Color(r, g, b, opacity);
        prim->TextureCrd(492, 66);
        prim->Vertex(static_cast<int>(overlay_left), static_cast<int>(overlay_top), 0);
        prim->TextureCrd(512, 96);
        prim->Vertex(static_cast<int>(overlay_left + 20.0f), static_cast<int>(overlay_top + 30.0f), 0);
        prim->End();
    }

    if (room->mark) {
        const float bob = 0.71875f * (6.0f * sinf(-room->mark_phase));
        mark_rect[mark_num].left = picture.left + 64.0f;
        mark_rect[mark_num].top = picture.top + 4.0f - bob;
        mark_rect[mark_num].right = 64.0f + bob;
        mark_rect[mark_num].bottom = 46.0f + bob;
        mark_num++;
    }

    const float tint = 128.0f * event_brightness;

    if (name_tex != nullptr && room->visited == 1) {
        for (int i = 0; i < 3; i++) {
            if (!(room->flag & (1 << (i + 1))) || kRoomLabelTexture[i][0] < 0) {
                continue;
            }

            const s16 *label = kRoomLabelTexture[i];
            prim->TextureMapEnable(1);
            prim->Begin(MG_PRIM_SPRITE);
            prim->Texture(name_tex);
            prim->Color(static_cast<int>(tint), static_cast<int>(tint), static_cast<int>(tint), opacity);
            mgRect<int> label_rect;
            label_rect.Set(label[0], label[1], label[2], label[3]);
            PrimQuad(prim, picture.left + static_cast<float>(kRoomLabelOffset[i].x), picture.top + static_cast<float>(kRoomLabelOffset[i].y),
                     label_rect);
            prim->End();
        }
    }
}

// Retail opens the floor map with no room under the cursor when the saved floor has no room cell and
// no floor is marked. Its step then reads the floor of that null cell (the debug flag editor at once,
// a directional key otherwise). On the PS2 that reads low RAM; natively it faults. Put the cursor on
// the entrance room, which is the floor retail's decide already falls back to with no selection.
PORT_OVERRIDE int DngTreeMapKey() {
    int result = 0;

    if (DngTreeMode == DNG_TREE_MODE_MAP) {
        if (CMenuTreePt->select_glid == nullptr && CMenuTreePt->key_arg_no == DNG_TREE_MAP_SCREEN_MAP &&
            CMenuTreePt->mode == MENU_ASK_MODE_NONE) {
            CMenuTreePt->select_glid = MenuDngMap->GetEntranceRoomGlid();
        }

        result = CMenuTreePt->Step();

        if (DngTreeMode == DNG_TREE_MODE_SAVE) {
            SetDngTreeFlag(1);
            SaveMapInfo(MenuDngMap->dng_no);
            NowProgramLoopNo = LOOP_DUNGEON;
            mgCMemory  save_stack;
            int        rest = MenuTreeMapStack.stGetRest();
            u_long128 *top = MenuTreeMapStack.stGetTop();
            save_stack.stSetBuffer(top, rest);
            MenuSaveInit(&save_stack, &CMenuTreePt->tex_block[3], MENU_OPEN_SAVE);
        }
    } else if (DngTreeMode == DNG_TREE_MODE_SAVE) {
        result = MenuSaveKey();

        if (result != 0) {
            SetDngTreeFlag(0);
            DngTreeMode = DNG_TREE_MODE_MAP;
            result = 0;
            CMenuTreePt->FadeInMenu(40, 0.0f);
            CMenuTreePt->mode = MENU_ASK_MODE_EXTEND;
            CMenuTreePt->step = 1;
            CMenuTreePt->MsgInit();
        }
    }

    return result;
}

// The floor information's monster page (floor_monsters.cpp) reads the keys before retail's step, which has no keys
// for the page's screen, and changes the information's state once that step has run.
PORT_OVERRIDE int CMenuTreeMap::Step() {
    const auto action = floor_monsters::BeforeStep(*this);
    const int  result = PortRetailStep();
    floor_monsters::AfterStep(*this, action);
    return result;
}

// The monster page and the prompt leading to it draw over retail's map and information.
PORT_OVERRIDE void CMenuTreeMap::Draw() {
    PortRetailDraw();
    floor_monsters::Draw(*this);
}
