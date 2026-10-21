#include <gtest/gtest.h>

#include "funcpoint.hpp"
#include "mg_memory.hpp"

TEST(FuncPointMemory, InitializeClearsTheAnimationEndpoint) {
    CFuncPoint point;
    for (float &v : point.anime.end) {
        v = 123.0f;
    }
    point.Initialize();
    for (float v : point.anime.end) {
        EXPECT_EQ(v, 0.0f);
    }
}

TEST(FuncPointMemory, CopyPreservesAllAnimationVectorsAndNames) {
    alignas(64) u_long128 storage[2048]{};
    mgCMemory             memory;
    memory.stSetBuffer(storage, 2048);
    CFuncPointMngr source;
    CFuncPointMngr destination;
    auto          *point = source.Add(FUNC_POINT_ANIME, &memory);
    ASSERT_NE(point, nullptr);
    char name[] = "moving-part";
    point->anime.parts_name = name;
    point->anime.piece_name = name;
    point->anime.frame_name = name;
    for (int i = 0; i < 4; ++i) {
        point->anime.param[i] = 10.0f + i;
        point->anime.speed[i] = 20.0f + i;
        point->anime.end[i] = 30.0f + i;
    }
    ASSERT_TRUE(source.Copy(destination, &memory));
    ASSERT_NE(destination.list[FUNC_POINT_ANIME], nullptr);
    auto &copy = destination.list[FUNC_POINT_ANIME]->data;
    EXPECT_EQ(copy.anime.parts_name, name);
    EXPECT_EQ(copy.anime.piece_name, name);
    EXPECT_EQ(copy.anime.frame_name, name);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(copy.anime.param[i], point->anime.param[i]);
        EXPECT_EQ(copy.anime.speed[i], point->anime.speed[i]);
        EXPECT_EQ(copy.anime.end[i], point->anime.end[i]);
    }
}
