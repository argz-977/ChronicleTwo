// nlohmann must precede the game headers, whose macros it would otherwise see.
// clang-format off
#include "platform/native_config.hpp"
// clang-format on

#include "common.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "dataread.hpp"
#include "dngfloor.hpp"
#include "menucommon.hpp"
#include "mg_memory.hpp"
#include "scriptinterpreter.hpp"
#include "stubs/dngfloor.hpp"

namespace {

// Retail's stack scratch, which flrtitle<n>.txt still has to fit.
constexpr int kRetailScratch = 0xA000;

/**
 * Room offset that a sub or boss room is drawn at.
 */
constexpr s16 kSubRoomOffsetY = -0x1A;

/**
 * Description of a passage cell of a dungeon_map document.
 */
struct MapRoot {
    int type;      /**< Kind of passage. */
    int shape;     /**< Shape the passage is drawn with. */
    int show_mark; /**< Non-zero to draw the passage's type mark once opened. */
};

/**
 * Description of a room cell of a dungeon_map document.
 */
struct MapRoom {
    int                                      floor_id; /**< Floor the room stands for. */
    int                                      order;    /**< Position of the floor along the dungeon. */
    std::optional<std::string>               name;     /**< CP932 name kept in DNGMAP_ROOM_INFO::unk_0, when given. */
    std::vector<int>                         link;     /**< Floor reached in each direction, as given. */
    std::optional<int>                       tex_no;   /**< Picture number; a negative one picks from a group at load. */
    std::optional<std::array<int, 4> >       key_room; /**< Floor that each direction's key leads to. */
    std::vector<std::optional<std::string> > options;  /**< Option name of each RI_OP, or nothing for an unreadable one. */
};

/**
 * One cell of a dungeon_map document.
 */
struct MapCell {
    int                    type;  /**< Kind of cell, a GLID_TYPE. */
    int                    x;     /**< Column of the cell. */
    int                    y;     /**< Row of the cell. */
    int                    unk_6; /**< Value stored in GLID_INFO::unk_6. */
    int                    unk_8; /**< Value stored in GLID_INFO::unk_8. */
    std::optional<MapRoot> root;  /**< Passage description, for an RT cell. */
    std::optional<MapRoom> room;  /**< Room description, for an RI cell. */
};

/**
 * Typed contents of a dungeon_map document.
 */
struct DungeonMap {
    int                  glid_w; /**< Width of the grid, in cells. */
    int                  glid_h; /**< Height of the grid, in cells. */
    std::vector<MapCell> cells;  /**< Cells of the grid, in script order. */
};

/**
 * Challenges of one floor, as RF_INFO gives them.
 */
struct FloorInfo {
    int                geostone;          /**< Non-zero when the floor holds a geostone. */
    int                fast_destroy_time; /**< Target clear time of the fast destroy challenge. */
    int                fishing;           /**< Fishing record test direction. */
    int                fishing_record;    /**< Size the fishing record test compares with. */
    int                seal;              /**< Seal on the floor, a DNGMAP_SEAL. */
    int                spheda;            /**< Non-zero when the floor has a spheda challenge. */
    std::array<int, 3> spheda_prize_item; /**< Prize of the spheda challenge, by rank. */
    std::array<int, 3> spheda_prize_num;  /**< Number given of each prize, by rank. */
};

/**
 * Practice condition of one floor, as RF_INFO2 gives it.
 */
struct FloorPractice {
    int type;  /**< Kind of practice condition. */
    int param; /**< Value the condition is tested against. */
};

/**
 * One floor of a dungeon_floors document.
 */
struct DungeonFloor {
    int                          floor;    /**< Floor number. */
    std::optional<FloorInfo>     info;     /**< Challenges, when given. */
    std::optional<FloorPractice> practice; /**< Practice condition, when given. */
};

/**
 * Largest cell count of a tree map grid, far above any retail dungeon, so its table size stays in range.
 */
constexpr std::size_t kGridCellMax = 0x10000;

/**
 * Parses a room description of a dungeon_map cell.
 */
MapRoom ParseMapRoom(const native_config::Json &room) {
    using namespace native_config;
    MapRoom out{.floor_id = Int(room, "floor_id"),
                .order = Int(room, "order"),
                .name = OptionalText(room, "name"),
                .link = room.contains("link") ? IntList(room, "link") : std::vector<int>{},
                .tex_no = OptionalInt(room, "tex_no"),
                .key_room = std::nullopt,
                .options = {}};
    // RI reads its third operand and discards it; it is only checked to be an integer.
    OptionalInt(room, "unused");
    if (room.contains("key_room")) {
        out.key_room = Ints<4>(room, "key_room");
    }
    for (const auto &option : OptionalArray(room, "options")) {
        out.options.push_back(GameText(option));
    }
    return out;
}

/**
 * Parses a dungeon_map document.
 */
DungeonMap ParseDungeonMap(const native_config::Json &document) {
    using namespace native_config;
    DungeonMap  map{.glid_w = Int(document, "glid_w"), .glid_h = Int(document, "glid_h"), .cells = {}};
    const auto &cells = Array(document, "cells");
    if (cells.size() > kGridCellMax) {
        throw std::runtime_error("tree map has too many cells");
    }
    for (const auto &cell : cells) {
        MapCell out{.type = Int(cell, "type"),
                    .x = Int(cell, "x"),
                    .y = Int(cell, "y"),
                    .unk_6 = Int(cell, "unk_6"),
                    .unk_8 = Int(cell, "unk_8"),
                    .root = std::nullopt,
                    .room = std::nullopt};
        if (cell.contains("root") && cell.contains("room")) {
            throw std::runtime_error("a cell is a root or a room, not both");
        }
        if (cell.contains("root")) {
            const auto &root = At(cell, "root");
            out.root = MapRoot{.type = Int(root, "type"), .shape = Int(root, "shape"), .show_mark = Int(root, "show_mark")};
        }
        if (cell.contains("room")) {
            out.room = ParseMapRoom(At(cell, "room"));
        }
        map.cells.push_back(std::move(out));
    }
    return map;
}

/**
 * Parses a dungeon_floors document.
 */
std::vector<DungeonFloor> ParseDungeonFloors(const native_config::Json &document) {
    using namespace native_config;
    std::vector<DungeonFloor> floors;
    for (const auto &floor : Array(document, "floors")) {
        DungeonFloor out{.floor = Int(floor, "floor"), .info = std::nullopt, .practice = std::nullopt};
        if (floor.contains("info")) {
            const auto &info = At(floor, "info");
            out.info = FloorInfo{.geostone = Int(info, "geostone"),
                                 .fast_destroy_time = Int(info, "fast_destroy_time"),
                                 .fishing = Int(info, "fishing"),
                                 .fishing_record = Int(info, "fishing_record"),
                                 .seal = Int(info, "seal"),
                                 .spheda = Int(info, "spheda"),
                                 .spheda_prize_item = Ints<3>(info, "spheda_prize_item"),
                                 .spheda_prize_num = Ints<3>(info, "spheda_prize_num")};
        }
        if (floor.contains("practice")) {
            const auto &practice = At(floor, "practice");
            out.practice = FloorPractice{.type = Int(practice, "type"), .param = Int(practice, "param")};
        }
        floors.push_back(out);
    }
    return floors;
}

/**
 * Gives the room flags of an option name, or -1 for a name RI_OP's table does not hold.
 */
int RoomOptionFlag(const std::optional<std::string> &name) {
    struct Option {
        std::string_view name; /**< Option name. */
        int              flag; /**< Flag the name sets. */
    };

    static constexpr Option kOptions[] = {
        {.name = "start", .flag = DNGMAP_ROOM_FLAG_START},
        {.name = "exit",  .flag = DNGMAP_ROOM_FLAG_EXIT },
        {.name = "boss",  .flag = DNGMAP_ROOM_FLAG_BOSS },
        {.name = "sub",   .flag = DNGMAP_ROOM_FLAG_SUB  },
    };
    for (const auto &option : kOptions) {
        if (name && *name == option.name) {
            return option.flag;
        }
    }
    return -1;
}

/**
 * Gives the picture number RI_TEX stores, picking one of a group's four pictures for a negative number.
 */
int RoomTexture(int texture_no) {
    if (texture_no >= 0) {
        return texture_no;
    }
    // Retail reads past its group table for a group above five; the port uses the last group.
    const int groups = static_cast<int>(std::size(offsetTable_911));
    const int texture_group = texture_no < -groups ? groups : -texture_no;
    return GetRandI(4) + offsetTable_911[texture_group - 1];
}

/**
 * Fills a room cell's description as RI, RI_LINK, RI_TEX, RI_KEYROOM and RI_OP do.
 */
void ApplyMapRoom(DNGMAP_ROOM_INFO &info, const MapRoom &room, mgCMemory *memory) {
    info.floor_id = static_cast<s8>(room.floor_id);
    info.order = static_cast<s8>(room.order);
    info.unk_0 = nullptr;
    if (room.name) {
        std::string name = *room.name;
        info.unk_0 = mgCopyString(name.data(), memory);
    }
    info.selectable = false;
    info.visited = false;
    info.flag = DNGMAP_ROOM_FLAG_ROOM;
    info.offset_y = 0;
    info.offset_x = 0;
    info.practice_type = -1;
    info.mark_phase = 0.0f;
    info.unk_4c = 0;

    // Retail writes past the four links for a longer list; the port keeps the first four.
    for (std::size_t i = 0; i < room.link.size() && i < std::size(info.link); ++i) {
        info.link[i] = static_cast<s16>(room.link[i]);
    }
    if (room.tex_no) {
        info.tex_no = static_cast<s8>(RoomTexture(*room.tex_no));
    }
    if (room.key_room) {
        for (std::size_t i = 0; i < std::size(info.key_room); ++i) {
            info.key_room[i] = static_cast<s16>((*room.key_room)[i]);
        }
    }
    for (const auto &option : room.options) {
        info.flag |= static_cast<u32>(DNGMAP_ROOM_FLAG_ROOM | RoomOptionFlag(option));
        if ((info.flag & DNGMAP_ROOM_FLAG_SUB) || (info.flag & DNGMAP_ROOM_FLAG_BOSS)) {
            info.offset_x = 0;
            info.offset_y = kSubRoomOffsetY;
        }
    }
}

/**
 * Builds the tree map grid as TREE_INFO and each cell's GI, RT and room tags do.
 */
void ApplyDungeonMap(CDngFloorManager &manager, const DungeonMap &map, mgCMemory *memory) {
    const auto count = static_cast<int>(map.cells.size());
    const u32  bytes = static_cast<u32>(count) * sizeof(GLID_INFO);
    manager.glid_w = static_cast<s16>(map.glid_w);
    manager.glid_h = static_cast<s16>(map.glid_h);
    const u32 blocks = (bytes & 0xF) != 0 ? (bytes >> 4) + 1 : bytes >> 4;
    manager.glid_info = new (memory->Alloc(static_cast<int>(blocks) + 2)) GLID_INFO[count];
    manager.glid_num = count;

    for (int i = 0; i < count; ++i) {
        const MapCell &cell = map.cells[i];
        GLID_INFO     &glid = manager.glid_info[i];
        glid.type = static_cast<s16>(cell.type);
        glid.x = static_cast<s16>(cell.x);
        glid.y = static_cast<s16>(cell.y);
        glid.unk_6 = static_cast<s16>(cell.unk_6);
        glid.unk_8 = static_cast<s16>(cell.unk_8);
        std::ranges::fill(glid.link_glid, nullptr);
        glid.blink = false;
        std::memset(&glid.room, 0, sizeof(glid.room));
        std::memset(&glid.root, 0, sizeof(glid.root));
        if (cell.root) {
            glid.root.type = static_cast<u8>(cell.root->type);
            glid.root.shape = static_cast<u8>(cell.root->shape);
            glid.root.show_mark = static_cast<u8>(cell.root->show_mark);
        } else if (cell.room) {
            ApplyMapRoom(glid.room, *cell.room, memory);
        }
    }
}

/**
 * Sets each listed floor's challenges and practice condition as RF_INFO and RF_INFO2 do.
 */
void ApplyDungeonFloors(CDngFloorManager &manager, const std::vector<DungeonFloor> &floors) {
    for (const auto &floor : floors) {
        DNGMAP_ROOM_INFO *info = manager.GetDngMapFloorInfo(floor.floor);
        if (info == nullptr) {
            continue;
        }
        if (floor.info) {
            const FloorInfo &values = *floor.info;
            info->geostone = static_cast<s8>(values.geostone);
            info->fast_destroy_time = values.fast_destroy_time;
            info->fishing = static_cast<s8>(values.fishing);
            info->fishing_record = static_cast<s16>(values.fishing_record);
            info->seal = static_cast<s8>(values.seal);
            info->spheda = static_cast<s8>(values.spheda);
            for (int i = 0; i < DNGMAP_SPHEDA_PRIZE_NUM; ++i) {
                info->spheda_prize_item[i] = static_cast<s16>(values.spheda_prize_item[i]);
                info->spheda_prize_num[i] = static_cast<s8>(values.spheda_prize_num[i]);
            }
        }
        if (const auto practice = floor.practice) {
            info->practice_type = static_cast<s8>(practice->type);
            info->practice_param = practice->param;
        }
    }
}

/**
 * Runs one table over the manager between the arena alignment and grid relation that AnalyzeFile wraps it in.
 */
template <typename Apply>
void AnalyzeTable(CDngFloorManager &manager, mgCMemory *memory, Apply apply) {
    tree_spi_stack = memory;
    tree_dngmap = &manager;
    memory->Align64();
    apply();
    memory->Align64();
    manager.RelationGlid();
}

/**
 * Reads a converted dmap<n>.cfg or dflr<n>.cfg table and runs it over the manager; false when it is not one.
 */
bool AnalyzeNativeTable(CDngFloorManager &manager, const std::string &path, mgCMemory *memory) {
    const std::string_view leaf = std::string_view(path).substr(path.rfind('/') + 1);
    if (leaf.starts_with("dflr")) {
        if (const auto floors = native_config::Read<std::vector<DungeonFloor> >(path, "dungeon_floors", ParseDungeonFloors)) {
            AnalyzeTable(manager, memory, [&] { ApplyDungeonFloors(manager, *floors); });
        }
        return true;
    }
    if (leaf.starts_with("dmap")) {
        if (const auto map = native_config::Read<DungeonMap>(path, "dungeon_map", ParseDungeonMap)) {
            AnalyzeTable(manager, memory, [&] { ApplyDungeonMap(manager, *map, memory); });
        }
        return true;
    }
    return false;
}

} // namespace

// The converted map and floor tables are native documents; any other buffer, such as flrtitle<n>.txt,
// is a text script that retail's tag table still reads.
PORT_OVERRIDE void CDngFloorManager::AnalyzeFile(char *data, int size, mgCMemory *memory) {
    const auto path = native_config::BufferPath(data);
    if (path && path->ends_with(".json") && AnalyzeNativeTable(*this, *path, memory)) {
        return;
    }
    AnalyzeTable(*this, memory, [&] {
        CScriptInterpreter interpreter;
        interpreter.SetTag(tree_map_tag);
        interpreter.SetScript(data, size);
        interpreter.Run();
    });
}

// The names are the converted ones: the build rewrites retail literals in ps2/src, not in a port
// override. The tables are read as documents; only flrtitle<n>.txt still goes through a scratch.
PORT_OVERRIDE void CDngFloorManager::LoadDataTable(int dungeon, mgCMemory *memory) {
    char menu_path[0x40];

    if (memory == nullptr) {
        return;
    }

    if (dungeon < 0 || dungeon >= DNGMAP_DUNGEON_NUM) {
        dungeon = 0;
    }

    Initialize();
    dng_no = dungeon;

    for (const char *table : {"menu/dngmap/dmap%d.cfg.json", "menu/dngmap/dflr%d.cfg.json"}) {
        char path[0x60];
        std::snprintf(path, sizeof path, table, dungeon);
        AnalyzeNativeTable(*this, path, memory);
    }

    // Room for the reader's sector padding and MenuCalcBufAlignment's rounding.
    const std::size_t bytes = static_cast<std::size_t>(kRetailScratch) + DATAREAD_SECTOR_SIZE + 64;
    const auto        scratch = std::make_unique<u_long128[]>(bytes / sizeof(u_long128) + 1);
    u_long128        *data = MenuCalcBufAlignment(scratch.get());

    std::snprintf(menu_path, sizeof menu_path, "flrtitle%d.txt", dungeon);
    const int size = LoadFileMenu(menu_path, data, MENU_FILE_LOAD_DIRECT);

    if (size > 0) {
        AnalyzeFile(reinterpret_cast<char *>(data), size, memory);
    }
}
