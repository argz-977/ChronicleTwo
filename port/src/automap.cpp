// nlohmann must precede the game headers, whose macros it would otherwise see.
// clang-format off
#include "platform/native_config.hpp"
// clang-format on

#include "common.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "automap.hpp"
#include "dng_main.hpp"
#include "gfx/gfx.hpp"
#include "maintex.hpp"
#include "mg_memory.hpp"
#include "mglib.hpp"
#include "scenesnd.hpp"
#include "test_hooks.hpp"

namespace {

constexpr gfx::UiAnchor kMiniMapAnchor = gfx::UiAnchor::Side(1, -1, true);

/**
 * Blocks of the room preset table, which retail spells as 0x62 for its 0x18-byte entries.
 */
constexpr int kRoomTableBlocks = static_cast<int>((sizeof(AUTOMAP_ROOM_INFO) * AUTOMAP_ROOM_INFO_MAX + 15) / 16) + 2;

/**
 * Part index and attribute of one cell of a room preset.
 */
using RoomCell = std::pair<s16, s16>;

/**
 * One room preset of a dungeon_rooms document.
 */
struct RoomPreset {
    int                                 id;    /**< Number the script gives the layout. */
    int                                 w;     /**< Width of the layout in cells. */
    int                                 h;     /**< Height of the layout in cells. */
    int                                 rate;  /**< Chance out of 100 that random room selection keeps the layout. */
    int                                 fixed; /**< Above zero keeps the layout out of random room selection. */
    std::vector<std::vector<RoomCell> > table; /**< Cells of each row, in script order. */
};

/**
 * Typed contents of a dungeon_rooms document.
 */
struct RoomPresets {
    std::optional<std::pair<float, float> > cell_size; /**< Cell width and depth, when the script sets them. */
    std::vector<RoomPreset>                 rooms;     /**< Room presets, in script order. */
};

/**
 * Largest cell count of one room preset, far above any retail layout, so its table size stays in range.
 */
constexpr int kRoomCellMax = 0x10000;

/**
 * Parses a dungeon_rooms document.
 */
RoomPresets ParseRoomPresets(const native_config::Json &document) {
    using namespace native_config;
    RoomPresets presets;
    const auto  cell_w = OptionalFloat(document, "cell_w");
    const auto  cell_d = OptionalFloat(document, "cell_d");
    if (cell_w.has_value() != cell_d.has_value()) {
        throw std::runtime_error("cell_w and cell_d go together");
    }
    if (cell_w) {
        presets.cell_size.emplace(*cell_w, *cell_d);
    }
    for (const auto &room : Array(document, "rooms")) {
        RoomPreset preset{.id = Int(room, "id"), .w = Int(room, "w"), .h = Int(room, "h"), .rate = Int(room, "rate"), .fixed = Int(room, "fixed"), .table = {}};
        if (preset.w < 0 || preset.h < 0 || (preset.h != 0 && preset.w > kRoomCellMax / preset.h)) {
            throw std::runtime_error("room size out of range");
        }
        for (const auto &row : Array(room, "table")) {
            if (!row.is_array()) {
                throw std::runtime_error("room table row is not an array");
            }
            auto &cells = preset.table.emplace_back();
            for (const auto &cell : row) {
                if (!cell.is_array() || cell.size() != 2) {
                    throw std::runtime_error("room cell needs a part and an attribute");
                }
                cells.emplace_back(static_cast<s16>(Int(cell[0])), static_cast<s16>(Int(cell[1])));
            }
        }
        presets.rooms.push_back(std::move(preset));
    }
    return presets;
}

/**
 * Fills one preset entry and its cell table as ROOM_ID, ROOM_SIZE, ROOM_RATE, ROOM_FIXED and RD do.
 */
void ApplyRoomPreset(AUTOMAP_ROOM_INFO &info, const RoomPreset &preset, mgCMemory *mem) {
    info.id = preset.id;
    const u32 bytes = static_cast<u32>(preset.w) * static_cast<u32>(preset.h) * 4;
    info.w = preset.w;
    info.h = preset.h;
    const u32 blocks = (bytes & 0xF) != 0 ? (bytes >> 4) + 1 : bytes >> 4;
    info.table = static_cast<s16 *>(::operator new[](bytes, mem->Alloc(static_cast<int>(blocks) + 2)));
    info.rate = preset.rate;
    info.fixed = preset.fixed;

    // A row of the wrong width is ignored, as retail's RD ignores it; rows past the height are dropped.
    s16 *cursor = info.table;
    int  rows = 0;
    for (const auto &row : preset.table) {
        if (std::cmp_not_equal(row.size(), preset.w)) {
            continue;
        }
        if (rows++ == preset.h) {
            break;
        }
        for (const auto &[part, attribute] : row) {
            *cursor++ = part;
            *cursor++ = attribute;
        }
    }
}

} // namespace

PORT_OVERRIDE void CMiniMapSymbol::SetMapInfo(CMap *new_map, CAutoMapParts *new_grid, int width, int height,
                                              float cell_width, float cell_depth) {
    if (!new_map) {
        return;
    }
    map = new_map;
    grid = new_grid;
    grid_w = width;
    grid_h = height;
    cell_w = cell_width;
    cell_d = cell_depth;
    int capacity = 0;
    parts_table = new_map->GetPlacPartsTable(&capacity);
    parts_num = 0;
    while (parts_table && parts_num < capacity && parts_table[parts_num].name[0]) {
        ++parts_num;
    }
    texture = mgTexManager.GetTexture("minimap1", -1);
    info = nullptr;
    char floor_name[sizeof(BattleAreaScene->map_name)];
    std::memcpy(floor_name, BattleAreaScene->map_name, sizeof floor_name);
    floor_name[sizeof floor_name - 1] = 0;
    if (std::strlen(floor_name) == 7) {
        floor_name[6] = 0;
    }
    for (auto &entry : MiniMapInfoData) {
        if (std::strcmp(entry.name, floor_name) == 0) {
            info = &entry;
            break;
        }
    }
    if (!info) {
        return;
    }
    for (int i = 0; i < parts_num; ++i) {
        auto &part = parts_table[i];
        part.minimap_tile = -1;
        // The final entry has an empty string, not a null pointer.
        for (std::size_t j = 0; j < std::size(PartsInfoData) && j < std::size(info->tile); ++j) {
            const char *name = PartsInfoData[j].name;
            if (!name || !*name) {
                break;
            }
            if (std::strcmp(name, part.parts_name) == 0) {
                part.minimap_tile = info->tile[j];
            }
        }
    }
}

PORT_OVERRIDE void CMiniMapSymbol::DrawSymbolOpen() {
    prim.Initialize(nullptr, nullptr);
    prim.Preset2D();
    prim.Begin(MG_PRIM_SPRITE);
    prim.Color(0x80, 0x80, 0x80, 0x60);
    prim.Texture(TEX_SystenFrame);
    prim.SetScirror(x - w / 2, y - h / 2, w, h);
    // Symbols other objects draw between Open and Close are part of the map, so they stay anchored with it.
    gfx::SetUiAnchor(kMiniMapAnchor);
}

PORT_OVERRIDE void CMiniMapSymbol::DrawSymbolClose() {
    prim.SetScirror(0, 0, mgScreenWidth - 1, mgScreenHeight - 1);
    prim.End();
    blink_cnt++;

    if (blink_cnt > 30) {
        blink_cnt = 0;
    }

    gfx::SetUiAnchor({});
}

PORT_OVERRIDE void CMiniMapSymbol::DrawSymbol_Chara(CCharacter2 *chara) {
    gfx::UiAnchorScope anchor(kMiniMapAnchor);

    float pos[4];
    float rot[4];

    if (chara == nullptr) {
        return;
    }

    if (BattleAreaScene->boss_map != 0) {
        return;
    }

    chara->GetPosition(pos);
    chara->GetRotation(rot);
    sceVu0SubVector(pos, pos, center);
    int        screen_x = x + static_cast<int>(16.0f * (pos[0] / cell_w)) + 8;
    int        screen_y = y + static_cast<int>(16.0f * (pos[2] / cell_d)) + 8;
    float      angle = rot[1];
    CPreSprite sprite;
    sprite.Initialize(nullptr, nullptr);
    sprite.Preset2D();
    sprite.Begin(MG_PRIM_TRIANGLE_STRIP);
    sprite.Color(0x80, 0x80, 0x80, 0x60);
    sprite.Texture(TEX_SystenFrame);
    sprite.SetScirror(x - w / 2, y - h / 2, w, h);
    int off_x = static_cast<int>(-7.0f * sinf(angle) - -6.0f * cosf(angle));
    int off_y = static_cast<int>(-6.0f * sinf(angle) + -7.0f * cosf(angle));
    sprite.TextureCrd(0xC4, 0xF2);
    sprite.Vertex(screen_x + off_x, screen_y + off_y, 0);
    off_x = static_cast<int>(-7.0f * sinf(angle) - 6.0f * cosf(angle));
    off_y = static_cast<int>(6.0f * sinf(angle) + -7.0f * cosf(angle));
    sprite.TextureCrd(0xD0, 0xF2);
    sprite.Vertex(screen_x + off_x, screen_y + off_y, 0);
    off_x = static_cast<int>(7.0f * sinf(angle) - -6.0f * cosf(angle));
    off_y = static_cast<int>(-6.0f * sinf(angle) + 7.0f * cosf(angle));
    sprite.TextureCrd(0xC4, 0x100);
    sprite.Vertex(screen_x + off_x, screen_y + off_y, 0);
    off_x = static_cast<int>(7.0f * sinf(angle) - 6.0f * cosf(angle));
    off_y = static_cast<int>(6.0f * sinf(angle) + 7.0f * cosf(angle));
    sprite.TextureCrd(0xD0, 0x100);
    sprite.Vertex(screen_x + off_x, screen_y + off_y, 0);
    sprite.SetScirror(0, 0, mgScreenWidth - 1, mgScreenHeight - 1);
    sprite.End();
}

PORT_OVERRIDE void CMiniMapSymbol::Draw(float *pos) {
    gfx::UiAnchorScope anchor(kMiniMapAnchor);

    if (map == nullptr) {
        return;
    }

    CMapParts *parts = parts_table;

    if (parts == nullptr) {
        return;
    }

    if (BattleAreaScene->boss_map != 0) {
        return;
    }

    sceVu0CopyVector(center, pos);
    const bool dimmed = (BattleAreaScene->minimap_reveal & MINIMAP_REVEAL_ROOMS) != 0;

    CPreSprite sprite;
    CPreSprite spare;
    float      part_pos[4];
    float      delta[4];
    sprite.Initialize(nullptr, nullptr);
    sprite.Preset2D();
    sprite.Begin(MG_PRIM_SPRITE);
    sprite.Texture(texture);
    sprite.Color(0x80, 0x80, 0x80, 0x60);
    sprite.SetScirror(x - w / 2, y - h / 2, w, h);

    for (int i = 0; i < parts_num; i++) {
        if (parts->name[0] != 0) {
            int tile = parts->minimap_tile;

            if (tile == -1) {
                parts++;
                continue;
            }

            int tile_u = (tile % 16) * 16;
            int tile_v = (tile / 16) * 16;
            parts->GetPosition(part_pos);
            sceVu0SubVector(delta, part_pos, center);
            float          rate_z = delta[2] / cell_d;
            float          rate_x = delta[0] / cell_w;
            float          screen_x = static_cast<float>(x) + 16.0f * rate_x;
            float          screen_y = y + rate_z * 16.0f;
            float          size_d = cell_d;
            CAutoMapParts *cells = grid;

            if (cells != nullptr) {
                s16            width = grid_w;
                CAutoMapParts *cell = &cells[width * static_cast<int>(part_pos[2] / size_d) + static_cast<int>(part_pos[0] / size_d)];

                if (cell->attr & AUTOMAP_ATTR_HIDE) {
                    parts++;
                    continue;
                }

                if (cell->visible) {
                    sprite.Color(0x80, 0x80, 0x80, 0x80);
                } else if (dimmed) {
                    sprite.Color(0x30, 0x30, 0x30, 0x80);
                } else {
                    sprite.Color(0x30, 0x30, 0x30, 0);
                }
            }

            int rect_x = static_cast<int>(screen_x);
            sprite.SetIRect(rect_x, static_cast<int>(screen_y), 16, 16, tile_u, tile_v);
        }

        parts++;
    }

    sprite.SetScirror(0, 0, mgScreenWidth - 1, mgScreenHeight - 1);
    sprite.End();
}

PORT_OVERRIDE void CAutoMapGen::SetupRoomInfo(char *name, int /*length*/, mgCMemory *mem) {
    // Build follows, so a fixed seed fixes the layout and the treasure and monster placement after it.
    if (const auto seed = GameSeed()) {
        srand(*seed);
    }
    room_info = new (mem->Alloc(kRoomTableBlocks)) AUTOMAP_ROOM_INFO[AUTOMAP_ROOM_INFO_MAX];

    for (int i = 0; i < AUTOMAP_ROOM_INFO_MAX; i++) {
        AUTOMAP_ROOM_INFO *preset = &room_info[i];
        preset->id = -1;
        preset->fixed = 0;
        preset->table = nullptr;
    }

    room_info_num = 0;
    const auto presets = native_config::ReadBuffer<RoomPresets>(name, "dungeon_rooms", ParseRoomPresets);
    if (!presets) {
        return;
    }
    if (presets->cell_size) {
        cell_w = presets->cell_size->first;
        cell_d = presets->cell_size->second;
    }
    // Retail writes past the table for more than AUTOMAP_ROOM_INFO_MAX rooms; the port keeps the first ones.
    for (const auto &preset : presets->rooms) {
        if (room_info_num == AUTOMAP_ROOM_INFO_MAX) {
            break;
        }
        ApplyRoomPreset(room_info[room_info_num++], preset, mem);
    }
}
