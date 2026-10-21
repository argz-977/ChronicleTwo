#include <gtest/gtest.h>

#include <cstring>

#include "character.hpp"
#include "event.hpp"
#include "event_func.hpp"
#include "mg_memory.hpp"
#include "mglib.hpp"
#include "runscript.hpp"
#include "scenesnd.hpp"
#include "stubs/event.hpp"
#include "swordeffect.hpp"

namespace {
class EventSwordMemory : public testing::Test {
protected:
    void SetUp() override {
        std::memcpy(program.magic, "SB2", 3);
        memory.stSetBuffer(storage, 1024);
        SetEventScript(reinterpret_cast<char *>(&program), nullptr, &memory);
        character.Initialize();
        scene.chara_num = 1;
        scene.AssignChara(0, &character, nullptr);
        EventScene = &scene;
        for (int i = 0; i < CHARA_SWORD_EFFECT_MAX; ++i) {
            character.sword_effect[i] = &effects[i];
        }
    }

    void TearDown() override {
        SetEventScript(nullptr, nullptr, nullptr);
        EventScene = nullptr;
    }

    void Colors(int slot) {
        RS_STACKDATA args[10]{};
        for (int i = 0; i < 10; ++i) {
            args[i].type = RS_INT;
            args[i].val.i = i + 10;
        }
        args[0].val.i = 0;
        args[1].val.i = slot;
        ASSERT_TRUE(EventScript.ext_func_table[EVENT_EXT_SWE_SET_COLOR](args, 10));
        for (int i = 0; i < CHARA_SWORD_EFFECT_MAX; ++i) {
            for (int lane = 0; lane < 4; ++lane) {
                EXPECT_EQ(effects[i].color0[lane], i == slot ? lane + 12 : 128);
                EXPECT_EQ(effects[i].color1[lane], i == slot ? lane + 16 : 128);
            }
        }
    }

    alignas(64) u_long128 storage[1024] {};
    mgCMemory         memory;
    RS_PROG_HEADER    program{};
    CScene            scene;
    CCharacter2       character;
    CSWordAfterEffect effects[CHARA_SWORD_EFFECT_MAX];
};
} // namespace

TEST_F(EventSwordMemory, FirstSlotKeepsItsExistingColorBehavior) { Colors(0); }

TEST_F(EventSwordMemory, SecondSlotUsesACompleteNativePointer) { Colors(1); }

TEST_F(EventSwordMemory, ThirdSlotDoesNotWriteTheSecondEffect) { Colors(2); }

TEST_F(EventSwordMemory, StartUsesTheSelectedEffectAndNamedFrames) {
    mgCFrame root;
    root.name = const_cast<char *>("root");
    character.CObjectFrame::frame = &root;
    effects[2].point_max = 8;
    RS_STACKDATA args[7]{};
    for (auto &arg : args) {
        arg.type = RS_INT;
    }
    args[0].val.i = 0;
    args[1].val.i = 2;
    args[2].val.s = root.name;
    args[3].val.s = root.name;
    args[4].val.i = 6;
    args[5].val.i = 12;
    args[6].val.i = 4;
    ASSERT_TRUE(EventScript.ext_func_table[EVENT_EXT_SWE_START_EFFECT](args, 7));
    EXPECT_EQ(effects[2].frame0, &root);
    EXPECT_EQ(effects[2].frame1, &root);
    EXPECT_EQ(effects[2].length, 6);
    EXPECT_EQ(effects[2].hold_time, 4);
    EXPECT_EQ(effects[2].write_index, 7);
    EXPECT_FLOAT_EQ(effects[2].fade_speed, 1.0f / 12.0f);
}

TEST_F(EventSwordMemory, TextureUsesTheSelectedSlotAndPreservesItsRectangle) {
    mgCTexture texture{};
    std::strcpy(texture.name, "trail");
    texture.block = 7;
    const int      bucket = mgTexManager.hash(texture.name);
    mgTEXTURE_HASH link{.texture = &texture, .next = mgTexManager.hash_table[bucket]};
    mgTexManager.hash_table[bucket] = &link;
    for (auto &effect : effects) {
        effect.texture = nullptr;
    }
    RS_STACKDATA args[8]{};
    for (auto &arg : args) {
        arg.type = RS_INT;
    }
    args[0].val.i = 0;
    args[1].val.i = 2;
    args[2].val.i = texture.block;
    args[3].val.s = texture.name;
    for (int i = 4; i < 8; ++i) {
        args[i].val.i = 10 + i;
    }
    const int result = EventScript.ext_func_table[EVENT_EXT_SWE_SET_TEXTURE](args, 8);
    mgTexManager.hash_table[bucket] = link.next;
    ASSERT_TRUE(result);
    EXPECT_EQ(effects[0].texture, nullptr);
    EXPECT_EQ(effects[1].texture, nullptr);
    EXPECT_EQ(effects[2].texture, &texture);
    EXPECT_EQ(effects[2].tex_block, 7);
    EXPECT_EQ(effects[2].tex_u, 14);
    EXPECT_EQ(effects[2].tex_v, 15);
    EXPECT_EQ(effects[2].tex_w, 16);
    EXPECT_EQ(effects[2].tex_h, 17);
}

TEST_F(EventSwordMemory, MissingCharacterOrEffectReturnsFailure) {
    RS_STACKDATA args[10]{};
    for (auto &arg : args) {
        arg.type = RS_INT;
    }
    args[0].val.i = 1;
    EXPECT_FALSE(EventScript.ext_func_table[EVENT_EXT_SWE_SET_COLOR](args, 10));
    args[0].val.i = 0;
    args[1].val.i = 1;
    character.sword_effect[1] = nullptr;
    EXPECT_FALSE(EventScript.ext_func_table[EVENT_EXT_SWE_SET_COLOR](args, 10));
}
