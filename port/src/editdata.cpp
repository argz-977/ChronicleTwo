// clang-format off
// nlohmann must precede the game headers, whose macros it would otherwise see.
#include "platform/native_config.hpp"
// clang-format on

#include "common.h"
#include "mw_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

#include "editdata.hpp"
#include "editmap.hpp"
#include "editriver.hpp"
#include "mg_memory.hpp"
#include "save_json.hpp"
#include "stubs/editdata.hpp"
#include "stubs/editmap.hpp"

namespace {

/** Polygon and drawing budget of every town: none, but small enough that the callers' sums cannot overflow. */
constexpr int kNoBudget = std::numeric_limits<int>::max() / 4;

/**
 * Index in a saved layout of an edit part slot, or -1 for a slot that was not saved.
 */
s16 Remapped(const std::vector<int> &remap, int slot) {
    if (slot < 0 || std::cmp_greater_equal(slot, remap.size())) {
        return -1;
    }
    return static_cast<s16>(remap[slot]);
}

/**
 * Culture points of one placed part as CultureAnalyzeParts counts them, given the parts resting on it.
 */
int CulturePoints(CEditMap &map, int no, const std::vector<int> &children) {
    CEditParts *parts = map.GetePlaceParts(no);
    if (parts == nullptr || parts->info == nullptr || parts->name[0] == '\0' || parts->state == EDIT_PARTS_STATE_NONE) {
        return 0;
    }
    switch (parts->info->cpoint[0]) {
        case EDIT_CULTURE_KIND_ALONE:
            return parts->info->cpoint[1];
        case EDIT_CULTURE_KIND_MAIN:
            break;
        default:
            return 0;
    }
    int total = parts->info->cpoint[1];
    if (map.CheckNormalPlaceParts(no) != 0) {
        for (int i = 0; i < map.edit_parts_max; ++i) {
            CEditParts *other = &map.edit_parts[i];
            if (map.CheckNormalPlaceParts(other) != 0 && parts->CheckTerritory(other) != 0 &&
                other->info->cpoint[0] == EDIT_CULTURE_KIND_TERRITORY) {
                total += other->info->cpoint[1];
            }
        }
    }
    for (const int child : children) {
        CEditParts *other = map.GetePlaceParts(child);
        if (other != nullptr && other->info != nullptr &&
            (other->info->cpoint[0] == EDIT_CULTURE_KIND_CHILD || other->info->cpoint[0] == EDIT_CULTURE_KIND_CHILD_2)) {
            total += other->info->cpoint[1];
        }
    }
    return total;
}

} // namespace

PORT_OVERRIDE int GetMaxPolyn(int /*map_no*/) {
    return kNoBudget;
}

PORT_OVERRIDE int GetMaxDrawMem(int /*map_no*/) {
    return kNoBudget;
}

// Retail's clear, which also forgets the layout kept for the record.
PORT_OVERRIDE void CEditData::Initialize() {
    std::memset(static_cast<void *>(this), 0, sizeof(*this));
    InitPlaceData();
    std::memset(static_cast<void *>(&analyze), 0, sizeof(analyze));
    PortClearTownLayout(*this);
}

// Retail's save of the town, into the layout kept for its record and with no limit on its parts or grids. The
// record's own layout arrays are cleared, as retail clears them before filling them.
PORT_OVERRIDE void CEditMap::SaveData(CEditData *data) {
    if (data == nullptr) {
        return;
    }
    data->InitPlaceData();
    TownLayout       layout;
    std::vector<int> remap(static_cast<std::size_t>(std::max(edit_parts_max, 0)), -1);
    for (int part_no = 0; part_no < edit_parts_max; ++part_no) {
        CEditParts *part = &edit_parts[part_no];
        if (part->name[0] == '\0' || part->info == nullptr) {
            continue;
        }
        TownPart saved;
        saved.id = part->info->id;
        saved.state = static_cast<s8>(part->state);
        float local[4];
        float rotation[4];
        float snapped[4];
        part->GetLocalPos(local);
        part->GetRotation(rotation);
        saved.angle = static_cast<s8>(ConvEditAngle(rotation[1]));
        GetEditPos(snapped, local);
        for (int k = 0; k < 3; ++k) {
            saved.pos[k] = static_cast<s16>(fptosi(snapped[k]));
        }
        for (int c = 0; c < EDIT_DATA_COLOR_MAX; ++c) {
            float color[4];
            if (part->GetColor(c, color) == 0) {
                continue;
            }
            for (int k = 0; k < 3; ++k) {
                saved.color[c][k] = static_cast<u8>(std::clamp(fptosi(128.0f * color[k]), 0, 0xFF));
            }
        }
        saved.house_no = part->house != nullptr ? static_cast<s32>(part->house - house) + 1 : 0;
        remap[part_no] = static_cast<int>(layout.parts.size());
        layout.parts.push_back(saved);
    }
    for (int slot = 0; slot < place_log_max; ++slot) {
        EditPlaceLog entry = place_log[slot];
        if (entry.parts_no < 0) {
            continue;
        }
        entry.parts_no = Remapped(remap, entry.parts_no);
        if (entry.base_no >= 0) {
            entry.base_no = Remapped(remap, entry.base_no);
        }
        layout.place_log.push_back(entry);
    }
    for (int i = 0; i < PortEditHouseCount(*this); ++i) {
        layout.houses.push_back({.npc_no = static_cast<s16>(house[i].npc_no[0])});
    }
    for (int k = 0; k < grid_max; ++k) {
        CEditGrid *river = grid[k];
        if (river == nullptr) {
            continue;
        }
        TownGrid saved{.num_x = river->num_x, .num_z = river->num_z, .pos = {}, .cells = {}};
        float    grid_pos[4];
        GetEditPos(grid_pos, river->origin);
        for (int i = 0; i < 3; ++i) {
            saved.pos[i] = static_cast<s16>(fptosi(grid_pos[i]));
        }
        for (int x = 0; x < river->num_x; ++x) {
            for (int z = 0; z < river->num_z; ++z) {
                saved.cells.push_back(river->GetFast(x, z)->river != 0 ? 1 : 0);
            }
        }
        layout.grids.push_back(std::move(saved));
    }
    PortTownLayout(*data) = std::move(layout);
}

// Retail's load of the town from the layout kept for its record: fixed parts keep their slot, the others are built
// and placed, and a part stays placed only while it rests, through the log, on a placed part.
PORT_OVERRIDE void CEditMap::LoadData(CEditData *data) {
    static const TownLayout empty;
    const TownLayout       *found = PortFindTownLayout(*data);
    const TownLayout       &layout = found != nullptr ? *found : empty;

    const auto log_slots = (layout.place_log.size() + EDIT_MAP_LOG_PER_PARTS - 1) / EDIT_MAP_LOG_PER_PARTS;
    PortEditReserveSlots(*this, static_cast<int>(std::max(layout.parts.size(), log_slots)));
    for (int m = 0; m < place_log_max; ++m) {
        place_log[m].parts_no = -1;
    }
    int log_count = 0;
    for (const EditPlaceLog &entry : layout.place_log) {
        if (entry.parts_no >= 0 && log_count < place_log_max) {
            place_log[log_count++] = entry;
        }
    }

    std::vector<int> remap(layout.parts.size(), -1);
    for (std::size_t index = 0; index < layout.parts.size(); ++index) {
        const TownPart &saved = layout.parts[index];
        CEditPartsInfo *info = saved.id != 0 ? GetePartsInfoAtID(saved.id) : nullptr;
        if (info == nullptr) {
            continue;
        }
        if ((info->attr & EDIT_PARTS_ATR_NO_PREVIEW) != 0) {
            remap[index] = static_cast<int>(index);
            continue;
        }
        const int no = BuildEditParts(info->edit_name);
        if (no < 0) {
            continue;
        }
        remap[index] = no;
        if (saved.state == EDIT_PARTS_STATE_NONE) {
            continue;
        }
        float       place_pos[4] = {static_cast<float>(saved.pos[0]), static_cast<float>(saved.pos[1]), static_cast<float>(saved.pos[2]), 1.0f};
        float       place_rot[4] = {0.0f, GetEditAngle(saved.angle), 0.0f, 0.0f};
        CEditParts *placed = GetePlaceParts(no);
        if (placed != nullptr && placed->GetPartsType() == EDIT_PARTS_TYPE_RIVER) {
            placed->state = EDIT_PARTS_STATE_RIVER;
            placed->SetPosition(0.0f, -10000.0f, 0.0f);
            continue;
        }
        placed = PlaceEditParts(no, nullptr, place_pos, place_rot, nullptr);
        if (placed == nullptr) {
            continue;
        }
        placed->state = saved.state == EDIT_PARTS_STATE_PLACED ? EDIT_PARTS_STATE_NONE : saved.state;
        if (saved.house_no > 0) {
            if (placed->house != nullptr) {
                std::memset(static_cast<void *>(placed->house), 0, sizeof(CEditHouse));
            }
            placed->house = PortEditHouse(*this, saved.house_no - 1);
            if (placed->house != nullptr) {
                placed->house->active = 1;
            }
        }
        for (int i = 0; i < EDIT_DATA_COLOR_MAX; ++i) {
            const auto &rgb = saved.color[i];
            if (rgb[0] > 0 && rgb[1] > 0 && rgb[2] > 0) {
                float color[4] = {static_cast<float>(rgb[0]) / 128.0f, static_cast<float>(rgb[1]) / 128.0f, static_cast<float>(rgb[2]) / 128.0f, 128.0f};
                placed->SetColor(i, color);
            }
        }
        placed->UpdateColor();
    }

    for (std::size_t i = 0; i < layout.houses.size(); ++i) {
        if (CEditHouse *town_house = PortEditHouse(*this, static_cast<int>(i))) {
            town_house->npc_no[0] = layout.houses[i].npc_no;
        }
    }

    for (int entry = 0; entry < log_count; ++entry) {
        if (place_log[entry].parts_no >= 0) {
            place_log[entry].parts_no = Remapped(remap, place_log[entry].parts_no);
        }
        if (place_log[entry].base_no >= 0) {
            place_log[entry].base_no = Remapped(remap, place_log[entry].base_no);
        }
    }

    // Retail's passes over the slots, each placed part placing the parts resting on it, until a pass changes nothing.
    std::vector<std::vector<int> > resting(static_cast<std::size_t>(std::max(edit_parts_max, 0)));
    for (int x = 0; x < log_count; ++x) {
        const int base = place_log[x].base_no;
        if (base >= 0 && base < edit_parts_max) {
            resting[base].push_back(x);
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (int k = 0; k < edit_parts_max; ++k) {
            CEditParts *part = &edit_parts[k];
            if (part->name[0] == '\0' || part->state != EDIT_PARTS_STATE_PLACED) {
                continue;
            }
            for (const int x : resting[k]) {
                CEditParts *lower = GetePlaceParts(place_log[x].parts_no);
                if (lower != nullptr && lower->state == EDIT_PARTS_STATE_NONE) {
                    changed = true;
                    lower->state = EDIT_PARTS_STATE_PLACED;
                    lower->ground = part->ground;
                }
            }
        }
    }

    std::size_t next_grid = 0;
    for (int j = 0; j < grid_max; ++j) {
        CEditGrid *river = grid[j];
        if (river == nullptr) {
            continue;
        }
        if (next_grid >= layout.grids.size()) {
            break;
        }
        const TownGrid &saved = layout.grids[next_grid++];
        if (river->num_x != saved.num_x || river->num_z != saved.num_z) {
            break;
        }
        for (int x = 0; x < river->num_x; ++x) {
            for (int z = 0; z < river->num_z; ++z) {
                if ((saved.cells[(static_cast<std::size_t>(x) * river->num_z) + z] & 1) != 0) {
                    river->SetRiver(x, z);
                }
            }
        }
    }
}

// Retail's culture analysis, with the parts resting on each part found through the placement log once.
PORT_OVERRIDE int CEditMap::CultureAnalyze(int /*mode*/) {
    std::vector<std::vector<int> > resting(static_cast<std::size_t>(std::max(edit_parts_max, 0)));
    for (int i = 0; i < place_log_max; ++i) {
        const EditPlaceLog &entry = place_log[i];
        if (entry.parts_no >= 0 && entry.base_no >= 0 && entry.base_no < edit_parts_max) {
            resting[entry.base_no].push_back(entry.parts_no);
        }
    }
    int total = 0;
    for (int i = 0; i < edit_parts_max; ++i) {
        total += CulturePoints(*this, i, resting[i]);
    }
    return total;
}

namespace {

/**
 * One named condition of a georama analysis.
 */
struct AnalysisCondition {
    int                        index;     /**< Condition number. */
    std::optional<std::string> name;      /**< Condition name, CP932. */
    std::optional<int>         geo_floor; /**< Floor whose geostone reveals it, when given. */
};

/**
 * One request of a georama analysis.
 */
struct AnalysisEntry {
    int                                        index;             /**< Request number. */
    int                                        unk_10;            /**< Value copied to EditAnalyzeDataSrc::unk_10. */
    std::optional<std::string>                 message;           /**< Request text, CP932. */
    std::optional<int>                         geo_floor;         /**< Floor whose geostone reveals it, when given. */
    std::optional<std::vector<int> >           condition_numbers; /**< Conditions the request needs, when given. */
    std::optional<std::optional<std::string> > on_parts;          /**< Parts shown while met, when given. */
    std::optional<std::optional<std::string> > off_parts;         /**< Parts hidden while met, when given. */
    std::optional<int>                         percent;           /**< Share of the town's future, when given. */
};

/**
 * Analysis of one georama map.
 */
struct AnalysisSource {
    int                            id;         /**< Map number of the analysis. */
    std::vector<AnalysisCondition> conditions; /**< Conditions in script order. */
    std::vector<AnalysisEntry>     entries;    /**< Requests in script order. */
};

/**
 * Parses a georama_analysis document.
 */
std::vector<AnalysisSource> ParseAnalysis(const native_config::Json &document) {
    using namespace native_config;
    std::vector<AnalysisSource> sources;
    for (const auto &source : Array(document, "sources")) {
        AnalysisSource parsed{.id = Int(source, "id"), .conditions = {}, .entries = {}};
        for (const auto &condition : Array(source, "conditions")) {
            parsed.conditions.push_back({.index = Int(condition, "index"), .name = OptionalText(condition, "name"), .geo_floor = OptionalInt(condition, "geo_floor")});
        }
        for (const auto &entry : Array(source, "entries")) {
            AnalysisEntry analysis{.index = Int(entry, "index"), .unk_10 = Int(entry, "unk_10"), .message = OptionalText(entry, "message"), .geo_floor = OptionalInt(entry, "geo_floor"), .condition_numbers = std::nullopt, .on_parts = std::nullopt, .off_parts = std::nullopt, .percent = OptionalInt(entry, "percent")};
            if (entry.contains("condition_numbers")) {
                analysis.condition_numbers = IntList(entry, "condition_numbers");
            }
            if (entry.contains("on_parts")) {
                analysis.on_parts = GameText(At(entry, "on_parts"));
            }
            if (entry.contains("off_parts")) {
                analysis.off_parts = GameText(At(entry, "off_parts"));
            }
            parsed.entries.push_back(std::move(analysis));
        }
        sources.push_back(std::move(parsed));
    }
    return sources;
}

/**
 * Copies optional game text into the analysis string memory.
 */
char *Copy(const std::optional<std::string> &text, mgCMemory *memory) {
    if (!text) {
        return nullptr;
    }
    std::string copy = *text;
    return mgCopyString(copy.data(), memory);
}

/**
 * Fills the analysis sources as the GEO_ANALYZE tags do.
 */
void ApplyAnalysis(const std::vector<AnalysisSource> &sources, mgCMemory *memory) {
    for (const auto &source : sources) {
        if (source.id < 0 || source.id >= EDIT_ANALYZE_MAP_MAX) {
            continue;
        }
        EditAnalyzeSrc *analysis = &AnalyzeSrc[source.id];
        analysis->Init();
        for (const auto &condition : source.conditions) {
            if (condition.index < 0 || condition.index >= EDIT_ANALYZE_CONDITION_MAX) {
                continue;
            }
            analysis->condition[condition.index] = Copy(condition.name, memory);
            if (condition.geo_floor) {
                analysis->geo_floor[condition.index] = static_cast<s16>(*condition.geo_floor);
            }
        }
        for (const auto &entry : source.entries) {
            if (entry.index < 0 || entry.index >= EDIT_ANALYZE_DATA_MAX) {
                continue;
            }
            EditAnalyzeDataSrc *data = &analysis->data[entry.index];
            data->unk_10 = entry.unk_10;
            data->message = Copy(entry.message, memory);
            data->con_no[0] = -1;
            if (entry.geo_floor) {
                data->geo_floor = static_cast<s16>(*entry.geo_floor);
            }
            // CON_NO leaves room for its -1 terminator, so it refuses a full list.
            if (entry.condition_numbers && entry.condition_numbers->size() < EDIT_ANALYZE_CON_NO_MAX) {
                std::size_t slot = 0;
                for (const int number : *entry.condition_numbers) {
                    data->con_no[slot++] = static_cast<s8>(number);
                }
                data->con_no[slot] = -1;
            }
            if (entry.on_parts) {
                data->on_parts = Copy(*entry.on_parts, memory);
            }
            if (entry.off_parts) {
                data->off_parts = Copy(*entry.off_parts, memory);
            }
            if (entry.percent) {
                data->percent = static_cast<s16>(*entry.percent);
            }
        }
    }
}

} // namespace

PORT_OVERRIDE void LoadEditAnalyzeData(int area_no, u_long128 * /*dest*/) {
    // Retail keeps the analysis strings in a function-local buffer of the same size.
    static u_long128 buffer[0x300];
    static mgCMemory memory;
    memory.stSetBuffer(buffer, 0x300);
    const std::string name = "geo" + std::to_string(area_no) + ".cfg.json";
    if (const auto sources = native_config::ReadGame<std::vector<AnalysisSource> >(name.c_str(), "georama_analysis", ParseAnalysis)) {
        ApplyAnalysis(*sources, &memory);
    }
    std::println("GeoData Remain = {}kbyte", ((memory.stack_size - memory.stack_used) * 16) / 1024);
}
