#include "common.h"

#include "gamedata.hpp"
#include "menucls1.hpp"
#include "menudraw_port.hpp"
#include "mglib_port.hpp"

PORT_OVERRIDE int CMenuItemUse::UseItem(CGameDataUsed *item, int kind, void *ptr) {
    CItemUseTarget target{};
    target.target.data = nullptr;
    target.SetPtr(kind, ptr);
    MenuUsedTarget.SetPtr(kind, ptr);
    return UseItem(item, &target);
}

// A menu form's message window parked past the screen is clipped to it, as the 4:3 picture is.
PORT_OVERRIDE void CDC2Mes::DrawMsg() {
    const mgPortScreenClipScope clip(MenuFormDrawsMessage(*this) && MenuMessageParked(*this));
    PortRetailDrawMsg();
}
