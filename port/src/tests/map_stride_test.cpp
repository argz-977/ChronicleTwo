#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "editmap.hpp"
#include "editparts.hpp"
#include "map.hpp"
#include "mg_frame.hpp"
#include "mg_memory.hpp"
#include "mg_visual.hpp"
#include "mglib.hpp"
#include "water.hpp"

TEST(MapStride, DrawPartsPointersFitBeforeTheNextAllocation) {
    std::vector<u_long128> storage(8192);
    mgCMemory              memory;
    memory.stSetBuffer(storage.data(), storage.size());
    CMap          map;
    constexpr int count = 33;
    map.SetPlacePartsBuff(&memory, count);
    ASSERT_NE(map.draw_parts, nullptr);
    const auto table_end = reinterpret_cast<std::uintptr_t>(map.draw_parts + count);
    const auto allocation_end = reinterpret_cast<std::uintptr_t>(memory.stGetTop());
    ASSERT_LE(table_end, allocation_end);
    for (int index = 0; index < count; ++index) {
        EXPECT_EQ(map.draw_parts[index], nullptr);
        map.draw_parts[index] = &map.place_parts[index];
    }
    u_long128 *next = memory.Alloc(16);
    std::memset(next, 0xA5, 16 * sizeof(u_long128));
    for (int index = 0; index < count; ++index) {
        EXPECT_EQ(map.draw_parts[index], &map.place_parts[index]);
    }
}

namespace {
class RecordingWater : public CWater {
public:
    std::vector<float> translations;

    int Draw(u_int * /*tag*/, float (*matrix)[4], mgCDrawManager * /*draw_manager*/) override {
        translations.push_back(matrix[3][0]);
        return 0;
    }
};
} // namespace

TEST(MapStride, WaterOverlayVisitsEveryPartPointer) {
    CMap           map;
    RecordingWater visual;
    CWaterFrame    frame;
    mgCFrameAttr   attr;
    attr.draw = MG_FRAME_DRAW_VISIBLE;
    attr.no_cull = 1;
    attr.point_light = 0;
    frame.attr = &attr;
    frame.visual = &visual;
    CMapWater placement;
    placement.Initialize();
    placement.SetScale(1.0f, 1.0f, 1.0f);
    placement.frame = &frame;
    CMapParts parts[2];
    parts[0].SetPosition(31.0f, 0.0f, 0.0f);
    parts[1].SetPosition(92.0f, 0.0f, 0.0f);
    CMapParts *pointers[] = {&parts[0], &parts[1]};
    placement.parts = pointers;
    placement.parts_num = 2;
    CWaterFrame *surfaces[] = {nullptr};
    map.water_surface = surfaces;
    map.water_surface_num = 1;
    map.water = &placement;
    map.water_num = 1;
    mgCTexture screen;
    mgCTexture overlay;
    map.DrawWater(nullptr, &screen, &overlay);
    EXPECT_EQ(visual.translations, (std::vector<float>{31.0f, 92.0f, 31.0f, 92.0f}));
}

TEST(MapStride, PlacedPartListUsesHostObjectsAndTheirNames) {
    CEditMap   map;
    CEditParts parts[5];
    map.edit_parts = parts;
    map.edit_parts_max = 5;
    for (auto &part : parts) {
        part.name[0] = '\0';
    }
    parts[1].name[0] = 'A';
    parts[4].name[0] = 'B';
    int output[4] = {-1, -1, -1, -1};
    EXPECT_EQ(map.GetePlaceIDList(output, 4), 2);
    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[1], 4);
    EXPECT_EQ(output[2], -1);
    output[0] = -1;
    EXPECT_EQ(map.GetePlaceIDList(output, 1), 1);
    EXPECT_EQ(output[0], 1);
    output[0] = -1;
    EXPECT_EQ(map.GetePlaceIDList(output, 0), 0);
    EXPECT_EQ(output[0], -1);
}
