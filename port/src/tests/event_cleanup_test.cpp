#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "dng_main.hpp"
#include "event.hpp"
#include "event_func.hpp"
#include "mainloop_port.hpp"
#include "mg_memory.hpp"
#include "platform/arena.hpp"
#include "runscript.hpp"
#include "stubs/event.hpp"

namespace {
struct CleanupProgram {
    RS_PROG_HEADER header{};
    RS_PROGDATA    entry{.no = 100, .func = offsetof(CleanupProgram, function)};
    funcdata       function{};
    vmcode_t       code[3]{
        {.op = RS_OP_PUSH_CONST, .arg1 = RS_CONST_INT, .arg2 = EVENT_EXT_DNG_EFFECT_ALL_CLEAR},
        {.op = RS_OP_EXT,        .arg1 = 1,            .arg2 = 0                             },
        {.op = RS_OP_END,        .arg1 = 0,            .arg2 = 0                             }
    };

    CleanupProgram() {
        std::memcpy(header.magic, "SB2", 3);
        header.main = offsetof(CleanupProgram, function);
        header.code = offsetof(CleanupProgram, code);
        header.prog = offsetof(CleanupProgram, entry);
        header.prog_num = 1;
    }
};

class EventCleanup : public testing::Test {
protected:
    void SetUp() override {
        previous_mode = PortLoopNo;
        previous_effects = FxScriptMan;
        memory.stSetBuffer(storage, 1024);
        MachineGun.active[0] = 1;
        MachineGun.col_prim_id[0] = 42;
        MachineGun.index = 3;
    }

    void TearDown() override {
        SetEventScript(nullptr, nullptr, nullptr);
        FxScriptMan = previous_effects;
        PortLoopNo = previous_mode;
    }

    void RunCleanup() {
        SetEventScript(reinterpret_cast<char *>(&program), nullptr, &memory);
        EXPECT_EQ(RunEvent(100, nullptr), 0);
        EXPECT_EQ(MachineGun.active[0], 0);
        EXPECT_EQ(MachineGun.col_prim_id[0], -1);
        EXPECT_EQ(MachineGun.index, 0);
    }

    int               previous_mode = 0;
    CEffectScriptMan *previous_effects = nullptr;
    alignas(64) u_long128 storage[1024] {};
    mgCMemory      memory;
    CleanupProgram program;
};
} // namespace

TEST_F(EventCleanup, TownClearWithNoDungeonManagerStillClearsGuns) {
    PortLoopNo = LOOP_EDIT;
    FxScriptMan = nullptr;
    RunCleanup();
}

TEST_F(EventCleanup, TownClearDoesNotUseAStaleDungeonManager) {
    PortLoopNo = LOOP_EDIT;
    FxScriptMan = reinterpret_cast<CEffectScriptMan *>(static_cast<std::uintptr_t>(0x123));
    RunCleanup();
}

TEST_F(EventCleanup, DungeonClearFreesItsLiveEffects) {
    PortLoopNo = LOOP_DUNGEON;
    static char key;
    auto       *buffer = reinterpret_cast<u_long128 *>(ArenaBlock(&key, 4096 * sizeof(u_long128)));
    mgCMemory   work;
    work.SetHeapMem(buffer, 4096);
    RS_PROG_HEADER effect_program{};
    std::memcpy(effect_program.magic, "SB2", 3);
    EFF_SPT_BASE base{};
    base.base_no = 7;
    base.work_size = 161;
    base.script = reinterpret_cast<char *>(&effect_program);
    CEffectScriptMan effects;
    effects.SetWorkBuffer(&work);
    effects.base[0] = &base;
    effects.base_num = 1;
    ASSERT_NE(effects.CreateEffSpt(7, 0, 1), nullptr);
    FxScriptMan = &effects;
    RunCleanup();
    EXPECT_EQ(effects.head, nullptr);
    EXPECT_EQ(effects.tail, nullptr);
    EXPECT_EQ(effects.slot[0][0], nullptr);
}

TEST_F(EventCleanup, SceneBindingsRetainEveryOtherRetailHandler) {
    CRunScript retail;
    SetEventFunc(&retail);
    SetEventScript(reinterpret_cast<char *>(&program), nullptr, &memory);
    ASSERT_EQ(EventScript.ext_func_num, retail.ext_func_num);
    for (int i = 0; i < retail.ext_func_num; ++i) {
        if (i == EVENT_EXT_DNG_EFFECT_ALL_CLEAR || i == EVENT_EXT_DIST_VECTOR ||
            i == EVENT_EXT_DIST_VECTOR2 || i == EVENT_EXT_AMG_GET_ATTR_STATUS ||
            i == EVENT_EXT_OBJS_SET_EOH_FRAME_POS || i == EVENT_EXT_GET_FISHINGTOURNAMENT_ETC ||
            i == EVENT_EXT_SWE_SET_COLOR ||
            i == EVENT_EXT_SWE_SET_TEXTURE || i == EVENT_EXT_SWE_START_EFFECT) {
            EXPECT_NE(EventScript.ext_func_table[i], retail.ext_func_table[i]);
        } else {
            EXPECT_EQ(EventScript.ext_func_table[i], retail.ext_func_table[i]) << i;
        }
    }
}
