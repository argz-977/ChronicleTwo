#include "common.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "editinfo.hpp"
#include "editmap.hpp"
#include "editparts.hpp"
#include "mapparts.hpp"
#include "mdslist.hpp"
#include "platform/arena.hpp"
#include "scene_documents.hpp"
#include "stubs/editmap.hpp"

namespace {

/** Quadwords a model heap must have free before a part's model is copied into it, as retail requires. */
constexpr int kPartBuildRest = 1000;

/** Definition attribute of parts that the river must keep carrying. */
constexpr u32 kRiverRelatedAttr = 0x80000;

/** Definition IDs that a removal counts. */
constexpr int kRemoveInfoIdMax = EDIT_REMOVE_INFO_ID_MAX;

/**
 * Host storage of one town's edit map beyond what CEditMap holds.
 */
struct EditMapStorage {
    std::deque<mgCMemory> heaps;            /**< Model heaps after parts_heap, at stable addresses. */
    std::size_t           heaps_in_use{};   /**< Leading entries of heaps holding models. */
    int                   heap_quadwords{}; /**< Quadwords of each model heap. */
    int                   house_count{};    /**< Houses handed out since the last clear. */
};

std::unordered_map<const CEditMap *, EditMapStorage> &Storages() {
    static std::unordered_map<const CEditMap *, EditMapStorage> storages;
    return storages;
}

EditMapStorage &Storage(const CEditMap &map) {
    return Storages()[&map];
}

/** Villagers made homeless by the removal in progress; RemoveInfo::house_npc points into it. */
std::vector<s32> &RemovedHouseNpcs() {
    static std::vector<s32> npcs;
    return npcs;
}

template <typename T>
T *SlotBlock(const void *owner, std::size_t count) {
    return reinterpret_cast<T *>(ArenaBlock(owner, count * sizeof(T)));
}

void BindHeap(mgCMemory &heap, int quadwords) {
    heap.SetHeapMem(SlotBlock<u_long128>(&heap, static_cast<std::size_t>(quadwords)), quadwords);
}

/**
 * Constructs the next edit part slot and its four placement log entries.
 */
void ConstructSlot(CEditMap &map) {
    // A town loaded where the last one was gets the same block back; its slots start as fresh memory would.
    CEditParts *slot = &map.edit_parts[map.edit_parts_max];
    std::memset(static_cast<void *>(slot), 0, sizeof(CEditParts));
    new (slot) CEditParts;
    for (int i = 0; i < EDIT_MAP_LOG_PER_PARTS; ++i) {
        map.place_log[map.place_log_max + i] = {.parts_no = -1, .base_no = -1};
    }
    ++map.edit_parts_max;
    map.place_log_max += EDIT_MAP_LOG_PER_PARTS;
}

/**
 * Model heap holding an address, or parts_heap when none does.
 */
mgCMemory &HeapOwning(CEditMap &map, const u_long128 *address) {
    auto &storage = Storage(map);
    for (std::size_t i = 0; i < storage.heaps_in_use; ++i) {
        mgCMemory &heap = storage.heaps[i];
        if (address >= heap.heap && address < heap.heap + heap.heap_size) {
            return heap;
        }
    }
    return map.parts_heap;
}

/**
 * Puts a model heap with room for a part's model into stack mode and returns it with the stack's start.
 */
std::pair<mgCMemory *, u_long128 *> HeapWithRoom(CEditMap &map) {
    const auto stack_with_room = [](mgCMemory &heap) -> u_long128 * {
        u_long128 *start = heap.StartStackMode(MG_STACK_MODE_LARGEST, 0);
        if (start != nullptr && heap.stGetRest() >= kPartBuildRest) {
            return start;
        }
        heap.EndStackMode();
        return nullptr;
    };
    if (u_long128 *start = stack_with_room(map.parts_heap)) {
        return {&map.parts_heap, start};
    }
    auto &storage = Storage(map);
    for (;;) {
        if (storage.heaps_in_use == storage.heaps.size()) {
            storage.heaps.emplace_back();
        }
        // Heaps out of use are empty: ClearAllParts cleared them, or CreateTable unbound them for a new size.
        mgCMemory &heap = storage.heaps[storage.heaps_in_use++];
        if (heap.heap_top == nullptr) {
            BindHeap(heap, storage.heap_quadwords);
        }
        if (u_long128 *start = stack_with_room(heap)) {
            return {&heap, start};
        }
    }
}

} // namespace

int PortEditHouseCount(const CEditMap &map) {
    return Storage(map).house_count;
}

CEditHouse *PortEditHouse(CEditMap &map, int index) {
    auto &storage = Storage(map);
    if (index < 0 || index >= kPortEditSlotCeiling) {
        return nullptr;
    }
    if (map.house == nullptr) {
        map.ClearHouse();
    }
    for (; storage.house_count <= index; ++storage.house_count) {
        std::memset(static_cast<void *>(&map.house[storage.house_count]), 0, sizeof(CEditHouse));
    }
    return &map.house[index];
}

void PortEditReserveSlots(CEditMap &map, int slots) {
    if (map.edit_parts == nullptr || map.place_log == nullptr) {
        return;
    }
    while (map.edit_parts_max < slots && map.edit_parts_max < kPortEditSlotCeiling) {
        ConstructSlot(map);
    }
}

PORT_OVERRIDE int CEditMap::GetePlaceIDList(int *output, int capacity) {
    int count = 0;
    for (int index = 0; index < edit_parts_max && count < capacity; ++index) {
        if (edit_parts[index].name[0] != '\0') {
            output[count++] = index;
        }
    }
    return count;
}

// Houses live in one block sized for every slot, so a part's house pointer never moves.
PORT_OVERRIDE void CEditMap::ClearHouse() {
    auto &storage = Storage(*this);
    house = SlotBlock<CEditHouse>(static_cast<const void *>(&house), kPortEditSlotCeiling);
    std::memset(static_cast<void *>(house), 0, static_cast<std::size_t>(storage.house_count) * sizeof(CEditHouse));
    storage.house_count = 0;
}

PORT_OVERRIDE CEditHouse *CEditMap::eNewHouseInfo() {
    const int count = PortEditHouseCount(*this);
    for (int i = 0; i < count; ++i) {
        if (house[i].active == 0) {
            return &house[i];
        }
    }
    return PortEditHouse(*this, count);
}

// The slots and their placement log are one block each, reserved for the slot ceiling and constructed as the town
// needs them: placed parts and their animations keep pointers to their slots, and retail walks the slots as an array.
PORT_OVERRIDE void CEditMap::CreateTable(mgCMemory * /*memory*/, int parts_max, int heap_size) {
    auto &storage = Storage(*this);
    // Every heap must fit at least one part's model, or a build would never find room.
    storage.heap_quadwords = std::max(heap_size * kArenaHeadroom, kPartBuildRest * 2);
    storage.heaps_in_use = 0;
    for (mgCMemory &heap : storage.heaps) {
        heap.Init();
    }
    BindHeap(parts_heap, storage.heap_quadwords);
    edit_parts = SlotBlock<CEditParts>(static_cast<const void *>(&edit_parts), kPortEditSlotCeiling);
    place_log = SlotBlock<EditPlaceLog>(static_cast<const void *>(&place_log), static_cast<std::size_t>(kPortEditSlotCeiling) * EDIT_MAP_LOG_PER_PARTS);
    edit_parts_max = 0;
    place_log_max = 0;
    PortEditReserveSlots(*this, parts_max);
}

PORT_OVERRIDE int CEditMap::eNewPlaceParts() {
    for (int i = 0; i < edit_parts_max; ++i) {
        if (edit_parts[i].name[0] == '\0') {
            return i;
        }
    }
    if (edit_parts == nullptr || edit_parts_max >= kPortEditSlotCeiling) {
        return EDIT_BUILD_NO_SLOT;
    }
    ConstructSlot(*this);
    return edit_parts_max - 1;
}

PORT_OVERRIDE void CEditMap::ClearAllParts() {
    auto &storage = Storage(*this);
    for (std::size_t i = 0; i < storage.heaps_in_use; ++i) {
        storage.heaps[i].ClearHeapMem();
    }
    storage.heaps_in_use = 0;
    PortRetailClearAllParts();
}

// Retail's build, with the model copied into whichever heap has room.
PORT_OVERRIDE int CEditMap::BuildEditParts(char *name) {
    CEditPartsInfo *info = GetePartsInfo(name);
    if (info == nullptr) {
        return EDIT_BUILD_NO_INFO;
    }
    const int   index = eNewPlaceParts();
    CEditParts *part = GetePlaceParts(index);
    if (part == nullptr) {
        return EDIT_BUILD_NO_SLOT;
    }
    CEditHouse *new_house = nullptr;
    if ((info->attr & EDIT_PARTS_ATR_TYPE_ONE) != 0) {
        new_house = eNewHouseInfo();
        if (new_house == nullptr) {
            part->Initialize();
            return EDIT_BUILD_NO_HOUSE;
        }
    }
    CMapParts *model = GetParts(info->parts_name);
    if (model == nullptr) {
        return EDIT_BUILD_NO_INFO;
    }
    const auto [heap, memory] = HeapWithRoom(*this);
    model->Copy(*part, heap);
    heap->EndStackMode();
    part->allocation_address = memory;
    part->info = info;
    part->SetPosition(0.0f, 0.0f, 0.0f);
    part->SetRotation(0.0f, 0.0f, 0.0f);
    part->SetScale(1.0f, 1.0f, 1.0f);
    part->CheckColorUpdate();
    if (new_house != nullptr) {
        std::memset(static_cast<void *>(new_house), 0, sizeof(CEditHouse));
        new_house->active = 1;
    }
    part->house = new_house;
    return index;
}

PORT_OVERRIDE int CEditMap::DeleteEditParts(int index) {
    CEditParts *part = GetePlaceParts(index);
    if (part == nullptr) {
        return 0;
    }
    if (part->house != nullptr) {
        std::memset(static_cast<void *>(part->house), 0, sizeof(CEditHouse));
    }
    if (part->allocation_address != nullptr) {
        HeapOwning(*this, part->allocation_address).Free(part->allocation_address);
    }
    part->Initialize();
    return 1;
}

namespace {

/**
 * One call of retail's recursive removal waiting on the calls it made.
 */
struct RemovalFrame {
    bool                 river{};      /**< Walking the slots for river parts the removed river no longer carries. */
    int                  index{};      /**< Slot removed, for a part. */
    std::array<float, 4> pos{};        /**< Position passed down to the calls this one makes. */
    std::size_t          outer{};      /**< Own log entry, or slot, being visited. */
    std::size_t          inner{};      /**< Dependent log entry being visited under the own entry. */
    bool                 own_active{}; /**< The own entry at outer matched and its dependents are being visited. */
};

/**
 * Retail's RemoveEditParts as an explicit stack, with each slot's log entries found through indexes built once.
 */
class Removal {
public:
    Removal(CEditMap &map, CEditMap::RemoveInfo *info) : map(map), info(info) {
        const auto slots = static_cast<std::size_t>(std::max(map.edit_parts_max, 0));
        by_part.resize(slots);
        by_base.resize(slots);
        if (map.place_log == nullptr) {
            return;
        }
        // Removal only ever frees entries, so these lists stay complete; entries are rechecked when visited.
        for (int n = 0; n < map.place_log_max; ++n) {
            const EditPlaceLog &entry = map.place_log[n];
            if (entry.parts_no >= 0 && std::cmp_less(entry.parts_no, slots)) {
                by_part[entry.parts_no].push_back(n);
            }
            if (entry.base_no >= 0 && std::cmp_less(entry.base_no, slots)) {
                by_base[entry.base_no].push_back(n);
            }
        }
    }

    int Run(int index, const float *pos) {
        const int result = Enter(index, pos);
        while (!stack.empty()) {
            Step();
        }
        return result;
    }

private:
    CEditMap                      &map;
    CEditMap::RemoveInfo          *info;
    std::vector<std::vector<int> > by_part;
    std::vector<std::vector<int> > by_base;
    std::vector<RemovalFrame>      stack;

    void Count(int id) {
        if (id >= 0 && id < kRemoveInfoIdMax && info != nullptr) {
            info->parts_num[id] += 1;
        }
    }

    void RefundPaint(CEditParts &part) {
        if (info == nullptr || info->color_num <= 0 || part.IsFence() != 0 || part.info == nullptr) {
            return;
        }
        float color[4];
        for (int c = 0; c < part.info->paint_num; ++c) {
            if (part.GetColor(c, color) == 0) {
                continue;
            }
            for (int j = 0; j < info->color_num; ++j) {
                if (EditPartsCmpColor(color, info->color[j]) != 0) {
                    info->paint_num[j] += map.RePaintNum(part.info->paint_used);
                    break;
                }
            }
        }
    }

    // The body of one retail call up to its own recursion; a call that recurses leaves a frame on the stack.
    int Enter(int index, const float *pos) {
        RemovalFrame frame;
        std::copy_n(pos, 4, frame.pos.begin());
        CEditParts *part = map.GetePlaceParts(index);
        if (part != nullptr) {
            if (info != nullptr && info->force == 0 && part->info != nullptr && (part->info->attr & EDIT_PARTS_ATR_NO_REMOVE) != 0) {
                return 0;
            }
            Count(part->GetInfoID());
            part->state = EDIT_PARTS_STATE_NONE;
            if (part->house != nullptr && part->house->npc_no[0] > 0 && info != nullptr) {
                auto &npcs = RemovedHouseNpcs();
                npcs.resize(static_cast<std::size_t>(info->house_num));
                npcs.push_back(part->house->npc_no[0]);
                info->house_npc = npcs.data();
                ++info->house_num;
            }
            RefundPaint(*part);
            map.DeleteEditParts(index);
            if (map.place_log_max == 0 || map.place_log == nullptr) {
                return 1;
            }
            frame.index = index;
            stack.push_back(frame);
            return 1;
        }
        if (map.RemoveRiver(frame.pos.data()) == 0) {
            return 0;
        }
        if (CEditPartsInfo *river = map.GetePartsInfoAtType(EDIT_PARTS_TYPE_RIVER)) {
            Count(river->id);
        }
        frame.river = true;
        stack.push_back(frame);
        return 1;
    }

    void Step() {
        RemovalFrame &frame = stack.back();
        if (frame.river) {
            StepRiver(frame);
        } else {
            StepPart(frame);
        }
    }

    // Retail's walk over every entry naming this slot, removing the parts resting on it under each one.
    void StepPart(RemovalFrame &frame) {
        const auto &own = by_part[frame.index];
        const auto &dependents = by_base[frame.index];
        while (frame.outer < own.size()) {
            const int     m = own[frame.outer];
            EditPlaceLog &entry = map.place_log[m];
            if (!frame.own_active) {
                if (entry.parts_no != static_cast<s16>(frame.index)) {
                    ++frame.outer;
                    continue;
                }
                frame.own_active = true;
                frame.inner = 0;
            }
            while (frame.inner < dependents.size()) {
                const int n = dependents[frame.inner++];
                if (n == m) {
                    continue;
                }
                const auto depth = stack.size();
                const auto pos = frame.pos;
                Enter(map.place_log[n].parts_no, pos.data());
                if (stack.size() != depth) {
                    return;
                }
            }
            map.place_log[m].parts_no = -1;
            frame.own_active = false;
            ++frame.outer;
        }
        stack.pop_back();
    }

    // Retail's walk over the slots after a river cell is removed, removing the parts it no longer carries.
    void StepRiver(RemovalFrame &frame) {
        while (std::cmp_less(frame.outer, map.edit_parts_max)) {
            const int   i = static_cast<int>(frame.outer++);
            CEditParts *candidate = &map.edit_parts[i];
            if (map.CheckNormalPlaceParts(candidate) == 0 || candidate->info == nullptr ||
                (candidate->info->attr & kRiverRelatedAttr) == 0) {
                continue;
            }
            float parts_pos[4];
            float parts_rot[4];
            candidate->GetPosition(parts_pos);
            candidate->GetRotation(parts_rot);
            if (map.CheckEditPartsOnRiver(candidate->info, parts_pos, parts_rot[1]) != 0) {
                continue;
            }
            const auto depth = stack.size();
            Enter(i, parts_pos);
            if (stack.size() != depth) {
                return;
            }
        }
        stack.pop_back();
    }
};

} // namespace

// Retail's removal without its recursion: a removed part takes every part resting on it, through the placement log,
// only when it has a log entry of its own; a river cell takes the river parts it carried.
PORT_OVERRIDE int CEditMap::RemoveEditParts(int index, float *pos, RemoveInfo *remove_info) {
    if (remove_info != nullptr && remove_info->house_num == 0) {
        RemovedHouseNpcs().clear();
    }
    return Removal(*this, remove_info).Run(index, pos);
}

namespace {

/**
 * Builds an editor part table as FIX_EPARTS_START or INIT_EPARTS_START and their rows do.
 */
ePlaceData *PlaceTable(const SceneEditTable &table, mgCMemory *stack) {
    if (table.count <= 0) {
        return nullptr;
    }
    const u32 size = table.count * sizeof(ePlaceData);
    const int quadwords = static_cast<int>((size & 0xF) != 0 ? (size >> 4) + 1 : size >> 4);
    auto     *places = new (stack->Alloc(quadwords + 2)) ePlaceData[table.count];
    if (places == nullptr) {
        return nullptr;
    }
    int index = 0;
    for (const auto &part : table.parts) {
        if (index >= table.count) {
            break;
        }
        ePlaceData *entry = &places[index++];
        entry->id = part.id;
        std::memcpy(entry->position, part.position.data(), sizeof(entry->position));
        entry->angle = part.angle;
    }
    return places;
}

/**
 * Makes a hidden river, mask or water piece of a model, as the river tags do.
 */
CMapPiece *RiverPiece(CEditMap &map, const SceneText &model, mgCMemory *stack, int attributes, const mgCFrameAttr &attr) {
    auto       *piece = new (stack->Alloc(0xD)) CMapPiece;
    std::string name = model.value_or(std::string{});
    CMdsInfo   *mds = map.SearchMDS(model ? name.data() : nullptr);
    if (mds != nullptr) {
        piece->AssignMds(mds);
        if (piece->frame != nullptr) {
            mgCFrameAttr copy = attr;
            piece->frame->SetAttrParam(copy, 1, attributes);
        }
    }
    return piece;
}

} // namespace

void PortApplyEditMapConfig(CEditMap *map, char *script, int /*size*/, mgCMemory *stack) {
    const auto config = ReadSceneConfig(script);
    if (!config || !config->georama) {
        return;
    }
    const auto &georama = *config->georama;
    if (georama.fixed_parts) {
        if (ePlaceData *table = PlaceTable(*georama.fixed_parts, stack)) {
            map->info_mngr.SeteFixPartsTable(table, georama.fixed_parts->count);
        }
    }
    mgCFrameAttr hidden;
    hidden.obj_alpha = 0.0f;
    hidden.clip_enable = 1;
    hidden.alpha_ref = 0;
    hidden.z_write = MG_ZBUF_NO_WRITE;
    const int hidden_attributes = MG_FRAME_ATTR_ALPHA_REF | MG_FRAME_ATTR_Z_WRITE | MG_FRAME_ATTR_CLIP | MG_FRAME_ATTR_OBJ_ALPHA;
    for (const auto &[index, parts, model] : georama.river_parts) {
        if (index < 0 || index >= EDIT_MAP_RIVER_PARTS_MAX) {
            continue;
        }
        std::string parts_name = parts.value_or(std::string{});
        map->river_parts[index] = map->GetParts(parts ? parts_name.data() : nullptr);
        map->river_piece[index] = RiverPiece(*map, model, stack, hidden_attributes, hidden);
    }
    for (const auto &model : georama.water_models) {
        mgCFrameAttr water;
        water.z_write = MG_ZBUF_NO_WRITE;
        water.clip_enable = 1;
        map->water_piece = RiverPiece(*map, model, stack, MG_FRAME_ATTR_Z_WRITE | MG_FRAME_ATTR_CLIP, water);
    }
    for (const auto &[index, model] : georama.mask_parts) {
        if (index < 0 || index >= EDIT_MAP_MASK_PIECE_MAX) {
            continue;
        }
        map->mask_piece[index] = RiverPiece(*map, model, stack, hidden_attributes, hidden);
    }
    if (georama.initial_parts) {
        if (ePlaceData *table = PlaceTable(*georama.initial_parts, stack)) {
            map->info_mngr.init_parts = table;
            map->info_mngr.init_parts_num = georama.initial_parts->count;
        }
    }
}
