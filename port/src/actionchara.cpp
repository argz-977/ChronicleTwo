#include "common.h"

#include "actionchara.hpp"
#include "menu_world.hpp"

// The player's effects are tagged while the dungeon pause frame is recorded, so the playerless picture drops them.
PORT_OVERRIDE void CActionChara::DrawEffect() {
    const menuworld::PlayerEffectScope scope(this == menuworld::PausingPlayer());
    PortRetailDrawEffect();
}
