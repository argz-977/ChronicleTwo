#include "common.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <vector>

#include "bounded_text.hpp"
#include "editmap.hpp"
#include "editmenu.hpp"
#include "editparts.hpp"
#include "mainloop.hpp"
#include "menudraw.hpp"
#include "savedata.hpp"
#include "stubs/editmenu.hpp"

namespace {

/**
 * Host storage behind the Georama menu's lists of placed parts, sized to the town.
 */
struct GeoramaLists {
    std::vector<s32>                     place_no;    /**< Storage of CMenuGeorama::place_no. */
    std::vector<std::array<char, 0x40> > place_name;  /**< Storage of CMenuGeorama::place_name. */
    std::vector<GEORAMA_PARTS_LIST_ITEM> placed_list; /**< Storage of CMenuGeorama::placed_list. */
    std::vector<GEORAMA_PARTS_LIST_ITEM> house_list;  /**< Storage of CMenuGeorama::house_list. */
};

GeoramaLists &Lists() {
    static GeoramaLists lists;
    return lists;
}

} // namespace

// Retail's list building, into lists as long as the town has part slots, with the culture points shown once.
PORT_OVERRIDE void CMenuGeorama::UpdateGeoramaPartsList() {
    if (MenuMainMapInfo == nullptr) {
        return;
    }

    // The placed list shows one line past its parts, so it always has room for an empty one.
    const auto capacity = std::max<std::size_t>(GEORAMA_PARTS_LIST_MAX, static_cast<std::size_t>(std::max(MenuMainMapInfo->edit_parts_max, 0)) + 1);
    auto      &lists = Lists();
    lists.place_no.assign(capacity, -1);
    lists.place_name.assign(capacity, {});
    lists.placed_list.assign(capacity, {});
    lists.house_list.resize(capacity);
    place_no = lists.place_no.data();
    place_name = reinterpret_cast<char (*)[0x40]>(lists.place_name.data());
    placed_list = lists.placed_list.data();
    house_list = lists.house_list.data();

    place_num = MenuMainMapInfo->GetePlaceIDList(place_no, static_cast<int>(capacity));
    std::memset(static_cast<void *>(stock_list), 0, sizeof(stock_list));
    stock_num = 0;
    placed_num = 0;
    house_num = 0;

    bool listed = false;
    for (int i = 0; i < place_num; ++i) {
        CEditParts *parts = MenuMainMapInfo->GetePlaceParts(place_no[i]);
        if (parts == nullptr) {
            continue;
        }
        CEditPartsInfo *info = parts->info;
        if (info == nullptr || (info->attr & EDIT_PARTS_ATR_NO_REFUND) != 0) {
            continue;
        }
        listed = true;
        bounded_text::Copy(lists.place_name[i], bounded_text::View(info->edit_name));
        if (parts->state == EDIT_PARTS_STATE_PLACED && parts->GetPartsType() == 1) {
            GEORAMA_PARTS_LIST_ITEM &entry = house_list[house_num++];
            entry.no = place_no[i];
            entry.num = 1;
            bounded_text::Copy(entry.name, bounded_text::View(info->edit_name));
        }
        if (parts->state != EDIT_PARTS_STATE_NONE) {
            continue;
        }
        GEORAMA_PARTS_LIST_ITEM &placed = placed_list[placed_num++];
        placed.no = place_no[i];
        placed.num = 1;
        bounded_text::Copy(placed.name, bounded_text::View(info->edit_name));
        // Retail counts the stock of unplaced parts here too, and then rebuilds the stock from the save below.
        auto *found = std::find_if(stock_list, stock_list + stock_num, [&](const GEORAMA_PARTS_LIST_ITEM &item) { return item.no == info->id; });
        if (found != stock_list + stock_num) {
            ++found->num;
        } else if (stock_num < GEORAMA_PARTS_LIST_MAX) {
            GEORAMA_PARTS_LIST_ITEM &stock = stock_list[stock_num++];
            stock.no = info->id;
            stock.num = 1;
            bounded_text::Copy(stock.name, bounded_text::View(info->edit_name));
        }
    }

    if (listed && cpview_form != nullptr) {
        int culture_arg = 0;
        if (GetSaveData()->GetBitFlag(SAVE_FLAG_HEIM_FIRE_OUT) == 0 && CMenuGeoPt->town_no == 3) {
            culture_arg |= 1;
        }
        cpview_form->SetNumber(const_cast<char *>("CP"), MenuMainMapInfo->CultureAnalyze(culture_arg));
    }

    CSaveData *save_data = GetSaveData();
    stock_num = 0;
    for (int i = 0; i < 0x80; ++i) {
        CEditPartsInfo *info = MenuMainMapInfo->GetePartsInfoAtID(i);
        if (info == nullptr) {
            continue;
        }
        const int owned = save_data->GetBuildPartsNum(info->id);
        if (owned <= 0) {
            continue;
        }
        GEORAMA_PARTS_LIST_ITEM &stock = stock_list[stock_num++];
        stock.no = info->id;
        stock.num = owned;
        if (info->edit_name != nullptr) {
            bounded_text::Copy(stock.name, bounded_text::View(info->edit_name));
        }
    }
    ArrangePartsList(0, 0);

    make_num = 0;
    for (int i = 0; i < GEORAMA_PARTS_LIST_MAX; ++i) {
        CEditPartsInfo *make_info = MenuMainMapInfo->GetePartsInfo(i);
        if (make_info == nullptr) {
            break;
        }
        if ((make_info->attr & EDIT_PARTS_ATR_NO_REFUND) != 0) {
            continue;
        }
        int       *made_end = PartsMakeOkTable + std::clamp(PartsMakeOkTableNum, 0, static_cast<int>(std::size(PartsMakeOkTable)));
        const bool made_ok = std::find(PartsMakeOkTable, made_end, make_info->id) != made_end;
        if (made_ok || make_info->edit_name == nullptr) {
            continue;
        }
        GEORAMA_PARTS_LIST_ITEM &make = make_list[make_num++];
        make.no = i;
        bounded_text::Copy(make.name, bounded_text::View(make_info->edit_name));
        make.num = make_info->polyn[0];
    }
    ArrangePartsList(1, 0);
    ArrangePartsList(2, 0);
}
