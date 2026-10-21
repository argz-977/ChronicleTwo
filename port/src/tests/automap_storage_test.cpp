#include <gtest/gtest.h>

#include <cstring>

#include "automap.hpp"
#include "dng_main.hpp"
#include "scenesnd.hpp"

TEST(AutoMapStorage, MiniMapUsesTheEmptyNameTerminatorInThePartTable) {
    DNG_BATTLE_AREA area{};
    auto           *previous = BattleAreaScene;
    BattleAreaScene = &area;
    std::strcpy(area.map_name, "d01f01");
    CMapParts parts[2];
    char      name[] = "way00";
    parts[0].SetName(name);
    parts[0].SetPartsName(name);
    CMap map;
    map.place_parts = parts;
    map.place_parts_max = 2;
    CMiniMapSymbol symbol;

    symbol.SetMapInfo(&map, nullptr, 0, 0, 320.0f, 320.0f);

    EXPECT_EQ(symbol.parts_num, 1);
    EXPECT_EQ(symbol.info, &MiniMapInfoData[0]);
    EXPECT_EQ(parts[0].minimap_tile, MiniMapInfoData[0].tile[0]);
    BattleAreaScene = previous;
}
