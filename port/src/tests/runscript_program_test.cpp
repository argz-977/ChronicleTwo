#include <gtest/gtest.h>

#include <cstddef>

#include "actionchara.hpp"
#include "runscript.hpp"

namespace {
struct ProgramTable {
    RS_PROG_HEADER header{};
    RS_PROGDATA    entries[2]{
        {.no = 7,   .func = 0},
        {.no = 150, .func = 0}
    };

    ProgramTable() {
        header.prog = offsetof(ProgramTable, entries);
        header.prog_num = 2;
    }
};
} // namespace

TEST(RunScriptProgram, UnloadedInterpreterHasNoOptionalPrograms) {
    CRunScript script;
    ASSERT_EQ(script.prog, nullptr);
    EXPECT_EQ(script.check_program(150), 0);
}

TEST(RunScriptProgram, LoadedProgramLookupAndDeletion) {
    ProgramTable program;
    CRunScript   script;
    script.prog = &program.header;
    EXPECT_EQ(script.check_program(7), 1);
    EXPECT_EQ(script.check_program(150), 1);
    EXPECT_EQ(script.check_program(151), 0);
    script.DeleteProgram();
    EXPECT_EQ(script.check_program(150), 0);
}

TEST(RunScriptProgram, TownCharacterResetsWithoutAnActionProgram) {
    CActionChara character{};
    character.Initialize(nullptr);
    ASSERT_EQ(character.script.prog, nullptr);
    character.add_speed = 3.0f;
    character.add_time = 5;
    character.blow_speed = 4.0f;
    character.blow_time = 7;
    character.ResetAction();
    EXPECT_EQ(character.add_speed, 0.0f);
    EXPECT_EQ(character.add_time, 0);
    EXPECT_EQ(character.blow_speed, 0.0f);
    EXPECT_EQ(character.blow_time, 0);
    EXPECT_EQ(character.prog_no, ACTION_PROG_MAIN);
}
