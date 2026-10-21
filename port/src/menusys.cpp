#include "common.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

#include "gamedata.hpp"
#include "menu_world.hpp"
#include "menucommon.hpp"
#include "menudraw.hpp"
#include "menumain.hpp"
#include "menusys.hpp"
#include "menusys_port.hpp"
#include "platform/config.hpp"
#include "stubs/menusys.hpp"

namespace {

// The item ids that CItemSelect::CheckUse reads after the request's use flag.
constexpr std::size_t kRequestIdMax = 10;
// MENU_INIT_ARG::end_code of the item picker's choice, which retail spells only as a number.
constexpr int kItemChosen = 7;

// Parts of the Item page's quick slots (the frame, icons, counts and cursor anchors), which the town and dungeon
// menus move toward the top of the screen.
constexpr std::array kQuickSlotNames = {"itemfrm", "item0", "item1", "item2", "item0num", "item1num",
                                        "item2num", "c_i0", "c_i1", "c_i2"};
// Logical pixels the quick slots move up.
constexpr float kQuickSlotLift = 20.0f;

// A quick slot part with the y retail last gave it, so a lift applies once however often the page is initialised.
struct QuickSlotPart {
    MENUFORMPARTS_TYPE *part = nullptr;
    float               retail_y = 0.0f;
    float               lifted_y = 0.0f;
};

std::array<QuickSlotPart, kQuickSlotNames.size()> quick_slot_parts{};

// Sets each quick slot part to its retail y, or to that y lifted when the page is the main menu's.
void SetQuickSlotLift(CMenuItemInfo &menu, bool lift) {
    CMenuPosDataForm *form = menu.view_form[0];
    if (form == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < kQuickSlotNames.size(); ++i) {
        MENUFORMPARTS_TYPE *part = form->GetPartInfo(const_cast<char *>(kQuickSlotNames[i]));
        if (part == nullptr) {
            continue;
        }
        QuickSlotPart &entry = quick_slot_parts[i];
        // A part whose y is not the last one lifted was rewritten by retail's init, so that y is the retail position.
        if (entry.part != part || part->y != entry.lifted_y) {
            entry = {part, part->y, part->y};
        }
        part->y = lift ? entry.retail_y - kQuickSlotLift : entry.retail_y;
        entry.lifted_y = part->y;
    }
}

} // namespace

int PortEventItemPlace(std::span<const int> param, std::span<CGameDataUsed> bag) {
    if (param.empty()) {
        return -1;
    }
    // param[0] says whether the chosen item is used up; the requested ids follow, ended by one that is not positive.
    const auto ids = param.subspan(1, std::min(kRequestIdMax, param.size() - 1));
    const auto end = std::ranges::find_if(ids, [](int id) { return id <= 0; });
    if (end - ids.begin() != 1) {
        return -1;
    }
    const int  wanted = ids.front();
    const auto found = std::ranges::find_if(bag, [wanted](CGameDataUsed &place) {
        // CItemSelect::SetPtrList leaves fitted Spectols and boiled items out of the picker.
        return place.item_no == wanted && place.GetSpectolNo() <= 0 && place.used_type != USED_ITEM_TYPE_BOILED;
    });
    return found == bag.end() ? -1 : static_cast<int>(found - bag.begin());
}

// With game.auto_use_event_item, an event that asks for one item the bag holds is handed it as if the picker had
// chosen it, and the picker never opens: MenuItemSelectKey then ends the menu at once.
PORT_OVERRIDE void MenuItemSelectInit(mgCMemory *stack, int *tex_block, int mode) {
    const std::span<CGameDataUsed> bag(MenuUserParam.used_data, MENU_BAG_PLACE_MAX);
    const bool                     automatic = mode == MENU_OPEN_USE_ITEM && ConfigGet().auto_use_event_item;
    const int                      place = automatic ? PortEventItemPlace(MenuArg.param, bag) : -1;
    if (place < 0) {
        PortRetailMenuItemSelectInit(stack, tex_block, mode);
        return;
    }

    MenuItemMainMemory.stSetBuffer(stack->stGetTop(), stack->stGetRest());
    ItemSelectPtr = nullptr;
    CGameDataUsed &item = bag[static_cast<std::size_t>(place)];
    MenuArg.end_code = kItemChosen;
    MenuArg.result[0] = item.item_no;
    MenuArg.result[1] = GetSameAdrressUserData(&item, 0);
    MenuArg.result[2] = 0;
    MenuArg.result[3] = 0;
    if (MenuArg.param[0] == 1) {
        item.DeleteNum(1);
    }
}

PORT_OVERRIDE void MenuItemSelectDraw() {
    if (ItemSelectPtr != nullptr) {
        PortRetailMenuItemSelectDraw();
    }
}

PORT_OVERRIDE void MenuItemDraw() {
    const menuworld::ItemDrawScope scope;
    PortRetailMenuItemDraw();
}

PORT_OVERRIDE int MenuItemInit(mgCMemory *stack, int *tex_block, int mode) {
    const int  result = PortRetailMenuItemInit(stack, tex_block, mode);
    const bool main_menu = MenuCommonInfo->open_type == MENU_OPEN_MAIN_TOWN || MenuCommonInfo->open_type == MENU_OPEN_MAIN_DUNGEON;
    SetQuickSlotLift(class_menu_item_info, main_menu);
    menuworld::ItemOpening();
    return result;
}
