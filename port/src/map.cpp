#include "common.h"

#include <algorithm>
#include <span>
#include <string_view>

#include "bounded_text.hpp"
#include "map.hpp"
#include "map_port.hpp"
#include "mapload.hpp"
#include "mapparts.hpp"
#include "mg_memory.hpp"

PORT_OVERRIDE void CMap::SetPlacePartsBuff(mgCMemory *memory, int capacity) {
    const std::size_t parts_bytes = sizeof(CMapParts) * capacity;
    place_parts = new (memory->Alloc((parts_bytes + 15) / 16 + 2)) CMapParts[capacity];
    const std::size_t pointers_bytes = sizeof(CMapParts *) * capacity;
    draw_parts = new (memory->Alloc((pointers_bytes + 15) / 16 + 2)) CMapParts *[capacity];
    place_parts_max = capacity;
    ClearPlaceParts();
}

namespace {

// Starlight Temple's item19 and item11 chests share opened flag 7, so opening one hides the other. No other chest of
// the town uses flag 9.
constexpr std::string_view kSharedFlagMap = "f02";
constexpr std::string_view kSharedFlagPoint = "item11";
constexpr int              kSharedFlag = 7;
constexpr int              kSeparateFlag = 9;

} // namespace

void PortSplitTreasureBoxFlags(CMap &map, std::string_view map_name) {
    if (map_name != kSharedFlagMap || map.tr_box == nullptr) {
        return;
    }

    for (CMapTreasureBox &box : std::span(map.tr_box, static_cast<std::size_t>(std::max(map.tr_box_num, 0)))) {
        if (box.flag_no == kSharedFlag && box.func_point != nullptr &&
            bounded_text::View(box.func_point->name) == kSharedFlagPoint) {
            box.flag_no = kSeparateFlag;
        }
    }
}
