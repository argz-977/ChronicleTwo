#include "common.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "bounded_text.hpp"
#include "editloop.hpp"
#include "editmap.hpp"
#include "mainloop.hpp"
#include "map_port.hpp"
#include "mapselect.hpp"
#include "mg_drawprim.hpp"
#include "platform/arena.hpp"
#include "scene.hpp"
#include "stubs/editloop.hpp"

void UpdateTrBoxFlag(int map_no);

namespace {

/** Sorted packets the town's frame work stack holds besides those of its Georama parts, as retail sizes it. */
constexpr std::size_t kTownPackets = 10000;

/** Sorted packets the work stack allows for each Georama part slot; a house with many materials takes about 100. */
constexpr std::size_t kPacketsPerPart = 256;

/** Quadwords the work stack grows to for the next frame after a frame used more than half of it. */
std::size_t g_work_quadwords_wanted = 0;

/**
 * Grows the frame work stack with the town's Georama part slots, and to twice its size once a frame has used more
 * than half of it: each sorted packet also takes a draw-table slot.
 */
void FitWorkBuffer() {
    CScene *scene = GetMainScene();
    CMap   *map = scene != nullptr ? scene->GetMap(scene->active_map) : nullptr;
    if (map == nullptr || std::strcmp(map->Iam(), "CEditMap") != 0) {
        return;
    }
    const auto        slots = static_cast<std::size_t>(std::max(static_cast<CEditMap *>(map)->edit_parts_max, 0));
    const std::size_t packet_bytes = (kTownPackets + (kPacketsPerPart * slots)) * (sizeof(mgSORT_PACKET) + sizeof(mgSORT_PACKET *));
    const std::size_t quadwords = std::max((packet_bytes + sizeof(u_long128) - 1) / sizeof(u_long128), g_work_quadwords_wanted);
    if (std::cmp_less_equal(quadwords, WorkBuffer.stack_size)) {
        return;
    }
    WorkBuffer.stSetBuffer(reinterpret_cast<u_long128 *>(ArenaBlock(&WorkBuffer, quadwords * sizeof(u_long128))), static_cast<int>(quadwords));
}

void NoteWorkBufferUse() {
    if (WorkBuffer.stack_used > WorkBuffer.stack_size / 2) {
        g_work_quadwords_wanted = std::max(g_work_quadwords_wanted, static_cast<std::size_t>(WorkBuffer.stack_size) * 2);
    }
}

} // namespace

// Retail's update, after chests that share another chest's opened flag get their own.
PORT_OVERRIDE void UpdateTrBoxFlag(int map_no) {
    CScene *scene = GetMainScene();
    PortSplitTreasureBoxFlags(*scene->GetMap(scene->active_map), bounded_text::View(GetMapName(map_no, nullptr)));
    PortRetailUpdateTrBoxFlag(map_no);
}

PORT_OVERRIDE int EditDraw() {
    FitWorkBuffer();
    const int drawn = PortRetailEditDraw();
    NoteWorkBufferUse();
    return drawn;
}
