#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "effscript.hpp"
#include "mg_memory.hpp"
#include "runscript.hpp"

void SetEffectScriptFunc();
int  SetEffectScript(CRunScript *script, char *program, mgCMemory *memory);

TEST(EffectDistance, LengthCallbackPreservesTheCallerAndWritesTheOutputReference) {
    // A write to the fourth vector lane clobbers a saved register in RelWithDebInfo; Debug's stack layout hides it.
    std::vector<u_long128> storage(256);
    mgCMemory              memory;
    memory.stSetBuffer(storage.data(), storage.size());
    RS_PROG_HEADER program = {};
    std::memcpy(program.magic, "SB2", 3);
    CRunScript script;
    SetEffectScriptFunc();
    ASSERT_TRUE(SetEffectScript(&script, reinterpret_cast<char *>(&program), &memory));

    RS_STACKDATA output = {};
    output.type = RS_FLOAT;
    output.val.f = -123.0f;
    RS_STACKDATA args[4] = {};
    args[0].type = RS_FLOAT;
    args[0].val.f = 3.0f;
    args[1].type = RS_INT;
    args[1].val.i = 4;
    args[2].type = RS_FLOAT;
    args[2].val.f = 12.0f;
    args[3].type = RS_PTR;
    args[3].val.p = &output;

    ASSERT_EQ(script.ext_func_table[EFF_EXT_DIST_VECTOR](args, 4), 1);
    EXPECT_FLOAT_EQ(output.val.f, 13.0f);
    EXPECT_FLOAT_EQ(args[0].val.f, 3.0f);
    EXPECT_EQ(args[1].val.i, 4);
    EXPECT_FLOAT_EQ(args[2].val.f, 12.0f);
    EXPECT_EQ(args[3].val.p, &output);
    EXPECT_EQ(script.stack_num, 64);
}

TEST(EffectDistance, BetweenTwoVectorsPreservesInputsAndOutputReference) {
    // A write to the fourth vector lane clobbers a saved register in RelWithDebInfo; Debug's stack layout hides it.
    std::vector<u_long128> storage(256);
    mgCMemory              memory;
    memory.stSetBuffer(storage.data(), storage.size());
    RS_PROG_HEADER program = {};
    std::memcpy(program.magic, "SB2", 3);
    CRunScript script;
    SetEffectScriptFunc();
    ASSERT_TRUE(SetEffectScript(&script, reinterpret_cast<char *>(&program), &memory));

    RS_STACKDATA output{};
    output.type = RS_FLOAT;
    output.val.f = -123;
    RS_STACKDATA args[7]{};
    const float  values[6] = {0, 0, 0, 3, 4, 12};
    for (int i = 0; i < 6; ++i) {
        args[i].type = RS_FLOAT;
        args[i].val.f = values[i];
    }
    args[6].type = RS_PTR;
    args[6].val.p = &output;
    ASSERT_EQ(script.ext_func_table[EFF_EXT_DIST_VECTOR2](args, 7), 1);
    EXPECT_FLOAT_EQ(output.val.f, 13);
    for (int i = 0; i < 6; ++i) {
        EXPECT_FLOAT_EQ(args[i].val.f, values[i]);
    }
    EXPECT_EQ(args[6].val.p, &output);
}
