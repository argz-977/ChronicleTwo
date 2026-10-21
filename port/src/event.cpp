#include "common.h"
#include "mw_runtime.h"

#include <algorithm>
#include <array>

#include "dng_main.hpp"
#include "event.hpp"
#include "event_func.hpp"
#include "event_vector_callbacks.hpp"
#include "mainloop.hpp"
#include "mainloop_port.hpp"
#include "mglib.hpp"
#include "runscript.hpp"
#include "event_text.hpp"
#include "script_guards.hpp"
#include "stubs/event.hpp"
#include "swordeffect.hpp"

namespace {
int SwordArgument(const RS_STACKDATA &value) {
    return value.type == RS_FLOAT ? fptosi(value.val.f) : value.val.i;
}

CSWordAfterEffect *SwordEffect(RS_STACKDATA *args, CCharacter2 **owner = nullptr) {
    auto     *character = GetCharacter(SwordArgument(args[0]));
    const int slot = SwordArgument(args[1]);
    if (character == nullptr || slot < 0 || slot >= CHARA_SWORD_EFFECT_MAX) {
        return nullptr;
    }
    if (owner != nullptr) {
        *owner = character;
    }
    return character->sword_effect[slot];
}

int SetSwordColors(RS_STACKDATA *args, int /*unused*/) {
    auto *effect = SwordEffect(args);
    if (effect == nullptr) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        effect->color0[i] = SwordArgument(args[2 + i]);
        effect->color1[i] = SwordArgument(args[6 + i]);
    }
    return true;
}

int SetSwordTexture(RS_STACKDATA *args, int /*unused*/) {
    auto *effect = SwordEffect(args);
    if (effect == nullptr) {
        return false;
    }
    const int block = SwordArgument(args[2]);
    auto     *texture = mgTexManager.GetTexture(args[3].val.s, block);
    if (texture == nullptr) {
        return false;
    }
    effect->SetTexture(block, texture, SwordArgument(args[4]), SwordArgument(args[5]),
                       SwordArgument(args[6]), SwordArgument(args[7]));
    return true;
}

int StartSwordEffect(RS_STACKDATA *args, int /*unused*/) {
    CCharacter2 *character = nullptr;
    auto        *effect = SwordEffect(args, &character);
    if (effect == nullptr || character->CObjectFrame::frame == nullptr) {
        return false;
    }
    auto *from = character->CObjectFrame::frame->SearchFrame(args[2].val.s);
    auto *to = character->CObjectFrame::frame->SearchFrame(args[3].val.s);
    if (from == nullptr || to == nullptr) {
        return false;
    }
    effect->StartEffect(from, to, SwordArgument(args[4]), SwordArgument(args[5]), SwordArgument(args[6]));
    return true;
}

int ClearDungeonEffects(RS_STACKDATA * /*unused*/, int /*unused*/) {
    RocketLauncher.Clear();
    for (int i = 0; i < 16; ++i) {
        MachineGun.active[i] = 0;
        MachineGun.col_prim_id[i] = -1;
    }
    MachineGun.index = 0;
    LaserGun.Clear();
    // Town events share this external, but only the dungeon owns FxScriptMan, which can be null on
    // first entry or point into a retired dungeon arena.
    if (PortLoopNo == LOOP_DUNGEON && FxScriptMan != nullptr) {
        FxScriptMan->AllClearEffSpt();
    }
    return true;
}
} // namespace

PORT_OVERRIDE void SetEventScript(char *program, char * /*unused*/, mgCMemory *memory) {
    // The interpreter's runtime arrays live here: the file arena's retail budget for the disc bytes
    // has no room for wider host pointers.
    static std::array<RS_STACKDATA, 128> values;
    static std::array<RS_CALLDATA, 512>  calls;
    if (program == nullptr || memory == nullptr) {
        EventScript.DeleteProgram();
        return;
    }
    EventScript.load(reinterpret_cast<RS_PROG_HEADER *>(program), values.data(), values.size(),
                     calls.data(), calls.size());
    SetEventFunc(&EventScript);
    // A private table replaces the slot-arithmetic externals and the dungeon external, whose
    // ownership does not hold in town mode.
    static std::array<int (*)(RS_STACKDATA *, int), EVENT_EXT_FUNC_NUM> functions;
    std::copy_n(EventScript.ext_func_table, EVENT_EXT_FUNC_NUM, functions.begin());
    functions[EVENT_EXT_DNG_EFFECT_ALL_CLEAR] = ClearDungeonEffects;
    ReplaceEventVectorCallbacks(functions.data());
    functions[EVENT_EXT_SWE_SET_COLOR] = SetSwordColors;
    functions[EVENT_EXT_SWE_SET_TEXTURE] = SetSwordTexture;
    functions[EVENT_EXT_SWE_START_EFFECT] = StartSwordEffect;
    ReplaceEventTextExternals(functions);
    script_guards::GuardEventExternals(functions);
    EventScript.ext_func(functions.data(), functions.size());
}
