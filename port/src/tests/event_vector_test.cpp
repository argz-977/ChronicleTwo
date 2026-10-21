#include <gtest/gtest.h>

#include <memory>

#include "dng_main.hpp"
#include "effscript.hpp"
#include "event.hpp"
#include "event_func.hpp"
#include "runscript.hpp"
#include "sceneseq.hpp"
#include "scenesnd.hpp"
#include "vector_scratch_probes.hpp"

namespace {
RS_STACKDATA Number(float value) {
    RS_STACKDATA s{};
    s.type = RS_FLOAT;
    s.val.f = value;
    return s;
}

RS_STACKDATA Integer(int value) {
    RS_STACKDATA s{};
    s.type = RS_INT;
    s.val.i = value;
    return s;
}

RS_STACKDATA Reference(RS_STACKDATA &value) {
    RS_STACKDATA s{};
    s.type = RS_PTR;
    s.val.p = &value;
    return s;
}
} // namespace

TEST(EventVector, HandleGettersReturnAllCoordinates) {
    CRunScript script;
    SetEventFunc(&script);
    mgCFrame frame;
    frame.SetPosition(3, 4, 12);
    frame.SetRotation(0.25f, 0.5f, 0.75f);
    ASSERT_TRUE(EventObjHandleMother.Set(0, EOH_TYPE_FRAME, &frame));
    for (int callback : {EVENT_EXT_EOH_GET_POS, EVENT_EXT_EOH_GET_ROT}) {
        RS_STACKDATA outputs[3] = {Number(-1), Number(-1), Number(-1)};
        RS_STACKDATA args[] = {Integer(0), Reference(outputs[0]), Reference(outputs[1]), Reference(outputs[2])};
        ASSERT_EQ(script.ext_func_table[callback](args, 4), 1);
        EXPECT_FLOAT_EQ(outputs[0].val.f, callback == EVENT_EXT_EOH_GET_POS ? 3 : 0.25f);
        EXPECT_FLOAT_EQ(outputs[1].val.f, callback == EVENT_EXT_EOH_GET_POS ? 4 : 0.5f);
        EXPECT_FLOAT_EQ(outputs[2].val.f, callback == EVENT_EXT_EOH_GET_POS ? 12 : 0.75f);
    }
}

TEST(EventVector, RandomCircleReturnsAllCoordinates) {
    CRunScript script;
    SetEventFunc(&script);
    RandomCircle.pos[0][0] = 3;
    RandomCircle.pos[0][1] = 4;
    RandomCircle.pos[0][2] = 12;
    RandomCircle.pos[0][3] = 1;
    RS_STACKDATA outputs[3] = {Number(-1), Number(-1), Number(-1)};
    RS_STACKDATA args[] = {Integer(0), Reference(outputs[0]), Reference(outputs[1]), Reference(outputs[2])};
    ASSERT_EQ(script.ext_func_table[EVENT_EXT_RANDOM_CIRCLE_GET_POS](args, 4), 1);
    EXPECT_FLOAT_EQ(outputs[0].val.f, 3);
    EXPECT_FLOAT_EQ(outputs[1].val.f, 4);
    EXPECT_FLOAT_EQ(outputs[2].val.f, 12);
}

TEST(EventVector, TreasurePositionIsDecodedBeforeMissingManagerReturns) {
    auto scene = std::make_unique<CScene>();
    auto *previous = EventScene;
    EventScene = scene.get();
    scene->battle_area.treasure_box = nullptr;
    CRunScript script;
    SetEventFunc(&script);
    RS_STACKDATA args[] = {Number(3), Number(4), Number(12), Number(0), Integer(0)};
    EXPECT_EQ(script.ext_func_table[EVENT_EXT_POST_TREASURE_BOX](args, 5), 0);
    EventScene = previous;
}

TEST(EventVector, ActualScratchDeclarationsHoldEveryWrittenLane) {
    EXPECT_TRUE(VectorScratchProbe::Event);
}
