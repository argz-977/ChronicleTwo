#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "automap.hpp"
#include "dataread.hpp"
#include "dataread_port.hpp"
#include "dng_event.hpp"
#include "dng_main.hpp"
#include "dngfloor.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "mg_memory.hpp"
#include "monster.hpp"
#include "platform/datapacks.hpp"
#include "savedatadungeon.hpp"

namespace {
using Json = nlohmann::json;

/**
 * Writes native dungeon documents into a temporary data root and reads them back through the retail loaders.
 */
class NativeDungeon : public testing::Test {
protected:
    std::filesystem::path  directory;     /**< Temporary data root. */
    std::filesystem::path  previous_root; /**< Datapack root restored after the fixture. */
    std::vector<u_long128> storage;       /**< Backing storage of the arena. */
    std::vector<u_long128> buffer;        /**< File buffer the retail loaders read from. */
    mgCMemory              memory;        /**< Arena receiving the loaded tables. */

    void SetUp() override {
        previous_root = datapacks::Root();
        directory = std::filesystem::temp_directory_path() / ("chronicle_native_dungeon_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory / "data");
        DataReadIndex(directory / "data");
        storage.resize(16384);
        buffer.resize(16384);
        memory.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
    }

    void TearDown() override {
        if (previous_root.empty()) {
            DataReadIndex(directory / "empty/data");
            datapacks::Clear();
        } else {
            DataReadIndex(previous_root.parent_path() / "data");
        }
        std::filesystem::remove_all(directory);
    }

    /**
     * Writes a native document of a kind and re-indexes the data root so the retail readers see it.
     */
    void Write(const std::string &name, const std::string &kind, const Json &body) {
        const auto path = directory / "data" / name;
        std::filesystem::create_directories(path.parent_path());
        Json document = body;
        document["schema_version"] = 2;
        document["kind"] = kind;
        std::ofstream(path, std::ios::binary) << document.dump(2);
        DataReadIndex(directory / "data");
    }

    /**
     * Reads a file into the loader buffer as the retail callers do, returning its size.
     */
    int Load(std::string name) {
        int size = 0;
        EXPECT_TRUE(LoadFile2(name.data(), buffer.data(), &size, LOAD_FILE_READ)) << name;
        return size;
    }

    /**
     * The loader buffer as the script pointer the retail loaders take.
     */
    char *Script() { return reinterpret_cast<char *>(buffer.data()); }
};

} // namespace

TEST_F(NativeDungeon, RoomPresetsFillTheirTablesAndKeepTheGridSize) {
    Write("dungeon/cfg_file/d01f01.cfg.json", "dungeon_rooms",
          {
              {"cell_w", 320.0                                                                                                     },
              {"cell_d", 160.0                                                                                                     },
              {"rooms",
               Json::array({{{"id", 3}, {"w", 2}, {"h", 2}, {"rate", 50}, {"fixed", 1}, {"table", {{{1, 0}, {2, 0}}, {{-1, 0}, {3, 4}}}}},
                            // A row of the wrong width is ignored, as RD ignores it.
                            {{"id", 4}, {"w", 1}, {"h", 1}, {"rate", 100}, {"fixed", 0}, {"table", {{{7, 7}, {8, 8}}, {{9, 5}}}}}})}
    });
    auto      generator = std::make_unique<CAutoMapGen>();
    const int size = Load("dungeon/cfg_file/d01f01.cfg.json");

    generator->SetupRoomInfo(Script(), size, &memory);

    EXPECT_FLOAT_EQ(generator->cell_w, 320.0f);
    EXPECT_FLOAT_EQ(generator->cell_d, 160.0f);
    ASSERT_EQ(generator->room_info_num, 2);
    const AUTOMAP_ROOM_INFO &first = generator->room_info[0];
    EXPECT_EQ(first.id, 3);
    EXPECT_EQ(first.w, 2);
    EXPECT_EQ(first.h, 2);
    EXPECT_EQ(first.rate, 50);
    EXPECT_EQ(first.fixed, 1);
    const s16 expected[] = {1, 0, 2, 0, -1, 0, 3, 4};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(first.table[i], expected[i]) << i;
    }
    EXPECT_EQ(generator->room_info[1].table[0], 9);
    EXPECT_EQ(generator->room_info[1].table[1], 5);
    EXPECT_EQ(generator->room_info[2].id, -1);
    EXPECT_EQ(generator->room_info[2].table, nullptr);
}

TEST_F(NativeDungeon, TreasureTablesStayInsideTheirLimits) {
    Json big_group = {
        {"group_id", 13 },
        {"item_num", 500},
        {"items",    {} }
    };
    for (int i = 0; i < 100; ++i) {
        big_group["items"].push_back({
            {"item_no", 200 + i},
            {"rank",    10     },
            {"num",     1      }
        });
    }
    Write("dungeon/cfg_file/tbox_d01.cfg.json", "treasure_boxes",
          {
              {"groups",
               Json::array({{{"group_id", 12}, {"item_num", 2}, {"items", {{{"item_no", 1}, {"rank", 15}, {"num", 1}}, {{"item_no", 22}, {"rank", 5}, {"num", 3}}}}}, big_group})},
              {"floor_start", 5                                                                                                                                                  },
              {"floors",      Json::array({{{"floor", 1}, {"group_id", {12, 13}}}, {{"floor", 500}, {"group_id", {12}}}})                                                        }
    });
    auto table = std::make_unique<TRESURE_BOX_FLOOR_INFO>();
    table->floor[1].group_num = -7;

    CreatTresuarBoxInfo(table.get(), Script(), Load("dungeon/cfg_file/tbox_d01.cfg.json"));

    EXPECT_EQ(table->group_num, 2);
    EXPECT_EQ(table->rank_max, 0);
    EXPECT_EQ(table->rank_min, 100);
    EXPECT_EQ(table->floor_start, 5);
    EXPECT_EQ(table->group[0].group_id, 12);
    EXPECT_EQ(table->group[0].item_num, 2);
    EXPECT_EQ(table->group[0].item[1].item_no, 22);
    EXPECT_EQ(table->group[0].item[1].num, 3);
    EXPECT_EQ(table->group[1].item_num, 96);
    EXPECT_EQ(table->group[1].item[95].item_no, 295);
    EXPECT_EQ(table->floor[1].group_num, 2);
    EXPECT_EQ(table->floor[1].group_id[1], 13);
}

TEST_F(NativeDungeon, MonsterFloorsFillOnlyTheCurrentFloor) {
    Json many = Json::array();
    for (int i = 0; i < 40; ++i) {
        many.push_back({
            {"monster_id", i    },
            {"param",      i * 2}
        });
    }
    Write("dungeon/cfg_file/mos_place0.cfg.json", "monster_floors",
          {
              {"floors",
               Json::array({{{"floor", 1}, {"monsters", {{{"monster_id", 8}, {"param", 804}}}}}, {{"floor", 2}, {"monsters", {{{"monster_id", 0}, {"param", 6}}}}}, {{"floor", 2}, {"monsters", many}}})}
    });
    auto  manager = std::make_unique<CMonsterMan>();
    auto  save = std::make_unique<CSaveDataDungeon>();
    auto *saved_manager = ActiveMonster;
    auto *saved_save = DngSaveDataDungeon;
    ActiveMonster = manager.get();
    DngSaveDataDungeon = save.get();
    save->stage_id = 0;
    save->floor_id[0] = 2;

    CreatMonsterFloorInfo(Script(), Load("dungeon/cfg_file/mos_place0.cfg.json"));

    // The second list for floor 2 restarts it, and stops at the list's capacity.
    EXPECT_EQ(manager->locate.num, MONSTER_LOCATE_MAX);
    EXPECT_EQ(manager->locate.monster_id[0], 0);
    EXPECT_EQ(manager->locate.param[31], 62);
    ActiveMonster = saved_manager;
    DngSaveDataDungeon = saved_save;
}

TEST_F(NativeDungeon, MonsterNamesFitTheirTableEntries) {
    BASE_MONSTER_TBL *first = GetMonsterTable(base_monster_define[0].id);
    BASE_MONSTER_TBL *second = GetMonsterTable(base_monster_define[1].id);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    char saved_first[sizeof(first->name)];
    char saved_second[sizeof(second->name)];
    std::memcpy(saved_first, first->name, sizeof saved_first);
    std::memcpy(saved_second, second->name, sizeof saved_second);
    const int saved_language = LanguageCode;
    LanguageCode = LANG_ENGLISH;
    Write("dungeon/cfg_file/mosdata1.cfg.json", "monster_names",
          {
              {"names", Json::array({{{"id", first->id}, {"name", "Native Rat"}}, {{"id", second->id}, {"name", std::string(40, 'x')}}, {{"id", -12345}, {"name", "Nobody"}}})}
    });

    LoadMonsterLanguage(1);

    EXPECT_STREQ(first->name, "Native Rat");
    // A name longer than the entry is ignored.
    EXPECT_EQ(std::memcmp(second->name, saved_second, sizeof saved_second), 0);
    std::memcpy(first->name, saved_first, sizeof saved_first);
    LanguageCode = saved_language;
}

TEST_F(NativeDungeon, DungeonMapsBuildTheGridAndFloorDetails) {
    Write("menu/dngmap/dmap0.cfg.json", "dungeon_map",
          {
              {"glid_w", 10                                                                                                                                                  },
              {"glid_h", 9                                                                                                                                                   },
              {"cells",
               Json::array({{{"type", 1},
                             {"x", 0},
                             {"y", 0},
                             {"unk_6", 0},
                             {"unk_8", 0},
                             {"room", {{"floor_id", 1}, {"order", 1}, {"name", "Entry"}, {"link", {-1, 2, -1, 1, 9}}, {"tex_no", 3}, {"key_room", {-1, 1, -1, 1}}, {"options", {"start"}}}}},
                            {{"type", 0}, {"x", 1}, {"y", 0}, {"unk_6", 1}, {"unk_8", 0}, {"root", {{"type", 2}, {"shape", 1}, {"show_mark", 1}}}},
                            {{"type", 1}, {"x", 2}, {"y", 0}, {"unk_6", 2}, {"unk_8", 0}, {"room", {{"floor_id", 2}, {"order", 2}, {"tex_no", -2}, {"options", {"boss"}}}}}})}
    });
    Write("menu/dngmap/dflr0.cfg.json", "dungeon_floors",
          {
              {"floors",
               Json::array({{{"floor", 1},
                             {"info", {{"geostone", 1}, {"fast_destroy_time", 9300}, {"fishing", -1}, {"fishing_record", 250}, {"seal", 2}, {"spheda", 1}, {"spheda_prize_item", {297, 273, 272}}, {"spheda_prize_num", {2, 3, 4}}}},
                             {"practice", {{"type", 5}, {"param", 7}}}},
                            {{"floor", 2}, {"practice", {{"type", 1}, {"param", 0}}}},
                            {{"floor", 30}, {"practice", {{"type", 1}, {"param", 0}}}}})}
    });
    CDngFloorManager manager{};

    manager.LoadDataTable(0, &memory);

    ASSERT_EQ(manager.glid_num, 3);
    EXPECT_EQ(manager.glid_w, 10);
    EXPECT_EQ(manager.glid_h, 9);
    const GLID_INFO &entry = manager.glid_info[0];
    EXPECT_EQ(entry.type, GLID_TYPE_ROOM);
    EXPECT_STREQ(entry.room.unk_0, "Entry");
    EXPECT_EQ(entry.room.link[1], 2);
    EXPECT_EQ(entry.room.link[3], 1);
    EXPECT_EQ(entry.room.tex_no, 3);
    EXPECT_EQ(entry.room.key_room[1], 1);
    EXPECT_EQ(entry.room.flag, static_cast<u32>(DNGMAP_ROOM_FLAG_ROOM | DNGMAP_ROOM_FLAG_START));
    EXPECT_EQ(entry.room.offset_y, 0);
    EXPECT_EQ(entry.room.geostone, 1);
    EXPECT_EQ(entry.room.fast_destroy_time, 9300);
    EXPECT_EQ(entry.room.fishing, -1);
    EXPECT_EQ(entry.room.fishing_record, 250);
    EXPECT_EQ(entry.room.seal, 2);
    EXPECT_EQ(entry.room.spheda_prize_item[2], 272);
    EXPECT_EQ(entry.room.spheda_prize_num[1], 3);
    EXPECT_EQ(entry.room.practice_type, 5);
    EXPECT_EQ(entry.room.practice_param, 7);
    EXPECT_EQ(manager.glid_info[1].root.type, 2);
    EXPECT_EQ(manager.glid_info[1].root.show_mark, 1);
    const DNGMAP_ROOM_INFO &boss = manager.glid_info[2].room;
    EXPECT_EQ(boss.unk_0, nullptr);
    EXPECT_GE(boss.tex_no, 4);
    EXPECT_LT(boss.tex_no, 8);
    EXPECT_EQ(boss.offset_y, -0x1A);
    EXPECT_EQ(boss.practice_type, 1);
    EXPECT_EQ(manager.GetDngMapFloorInfo(1), &manager.glid_info[0].room);
}

TEST_F(NativeDungeon, FloorTitleTextStillRunsThroughTheScript) {
    Write("menu/dngmap/dmap0.cfg.json", "dungeon_map",
          {
              {"glid_w", 1                                                                                                                      },
              {"glid_h", 1                                                                                                                      },
              {"cells",  Json::array({{{"type", 1}, {"x", 0}, {"y", 0}, {"unk_6", 0}, {"unk_8", 0}, {"room", {{"floor_id", 4}, {"order", 4}}}}})}
    });
    CDngFloorManager manager{};
    manager.LoadDataTable(0, &memory);
    ASSERT_EQ(manager.glid_num, 1);
    char title[] = "RI_TITLE 4,\"Floor Four\";\n";

    manager.AnalyzeFile(title, static_cast<int>(std::strlen(title)), &memory);

    EXPECT_STREQ(manager.glid_info[0].room.title, "Floor Four");
}
