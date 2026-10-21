#include <gtest/gtest.h>

#include "gameutil.hpp"
#include "stubs/gameutil.hpp"

namespace {

void SetPoint(float *dst, float x, float y, float z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    dst[3] = 1.0f;
}

// Horizontal triangle at height y that contains (0, y, 0).
CCPoly Floor(float y) {
    CCPoly poly = {};
    SetPoint(poly.vertex[0], -10.0f, y, -10.0f);
    SetPoint(poly.vertex[1], 10.0f, y, -10.0f);
    SetPoint(poly.vertex[2], 0.0f, y, 10.0f);
    SetPoint(poly.normal, 0.0f, 1.0f, 0.0f);
    return poly;
}

CollisionInfo MakeInfo(CCPoly *polys, int count) {
    CollisionInfo info = {};
    info.count = count;
    info.polys = polys;
    return info;
}

} // namespace

TEST(GameUtilAsm, TestVUnewWeightsTransformedVertexIntoAccumulator) {
    float matrix[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    float vertex[4] = {1.0f, 2.0f, 3.0f, 1.0f};
    float weight[4] = {0.5f, 0.0f, 0.0f, 0.0f};
    float accum[4] = {10.0f, 20.0f, 30.0f, 1.0f};
    float out[4] = {};

    testVUnew(matrix, vertex, weight, accum, out);

    EXPECT_FLOAT_EQ(accum[0], 10.5f);
    EXPECT_FLOAT_EQ(accum[1], 21.0f);
    EXPECT_FLOAT_EQ(accum[2], 31.5f);
    EXPECT_FLOAT_EQ(accum[3], 1.0f);
    EXPECT_FLOAT_EQ(out[0], 10.5f);
    EXPECT_FLOAT_EQ(out[1], 21.0f);
    EXPECT_FLOAT_EQ(out[2], 31.5f);
    EXPECT_FLOAT_EQ(out[3], 1.0f);
}

TEST(GameUtilAsm, TestVUnewTakesTranslationRowAsRowVectorTimesMatrix) {
    float matrix[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {5.0f, 6.0f, 7.0f, 1.0f},
    };
    float vertex[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float weight[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float accum[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float out[4] = {};

    testVUnew(matrix, vertex, weight, accum, out);

    EXPECT_FLOAT_EQ(out[0], 5.0f);
    EXPECT_FLOAT_EQ(out[1], 6.0f);
    EXPECT_FLOAT_EQ(out[2], 7.0f);
    EXPECT_FLOAT_EQ(out[3], 1.0f);
}

TEST(GameUtilAsm, TestVUnewAddsRowTwoLastColumnToAccumulatorW) {
    float matrix[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f },
        {0.0f, 1.0f, 0.0f, 0.0f },
        {0.0f, 0.0f, 1.0f, 0.25f},
        {0.0f, 0.0f, 0.0f, 1.0f },
    };
    float vertex[4] = {2.0f, 2.0f, 2.0f, 1.0f};
    float weight[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float accum[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float out[4] = {};

    testVUnew(matrix, vertex, weight, accum, out);

    EXPECT_FLOAT_EQ(out[0], 0.0f);
    EXPECT_FLOAT_EQ(out[1], 0.0f);
    EXPECT_FLOAT_EQ(out[2], 0.0f);
    EXPECT_FLOAT_EQ(out[3], 1.25f);
}

TEST(GameUtilAsm, CheckHitReturnsMinusOneWhenNoPolygonIsCrossed) {
    CCPoly        polys[1] = {Floor(0.0f)};
    CollisionInfo info = MakeInfo(polys, 1);
    float         from[4] = {50.0f, 10.0f, 50.0f, 1.0f};
    float         to[4] = {50.0f, -10.0f, 50.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 1, 0), -1);
}

TEST(GameUtilAsm, CheckHitReturnsFloorIntersectionForCrossingSegment) {
    CCPoly        polys[1] = {Floor(0.0f)};
    CollisionInfo info = MakeInfo(polys, 1);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 1, 0), 0);
    EXPECT_FLOAT_EQ(hit[0], 0.0f);
    EXPECT_FLOAT_EQ(hit[1], 0.0f);
    EXPECT_FLOAT_EQ(hit[2], 0.0f);
}

TEST(GameUtilAsm, CheckHitClosestPicksNearestPolygonToOrigin) {
    CCPoly        polys[2] = {Floor(0.0f), Floor(5.0f)};
    CollisionInfo info = MakeInfo(polys, 2);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 1, 0), 1);
    EXPECT_FLOAT_EQ(hit[1], 5.0f);
}

TEST(GameUtilAsm, CheckHitNotClosestTakesFirstCrossedPolygon) {
    CCPoly        polys[2] = {Floor(0.0f), Floor(5.0f)};
    CollisionInfo info = MakeInfo(polys, 2);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 0, 0), 0);
    EXPECT_FLOAT_EQ(hit[1], 0.0f);
}

TEST(GameUtilAsm, CheckHitSkipsPolygonWhoseIgnoreMaskMatches) {
    CCPoly polys[2] = {Floor(0.0f), Floor(5.0f)};
    polys[0].ignore_mask = 1;
    CollisionInfo info = MakeInfo(polys, 2);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 1, 1), 1);
}

TEST(GameUtilAsm, CheckHitMissesSegmentThatStaysAbovePlane) {
    CCPoly        polys[1] = {Floor(0.0f)};
    CollisionInfo info = MakeInfo(polys, 1);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, 5.0f, 0.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 1, 0), -1);
}

TEST(GameUtilAsm, CheckHitNullCollisionReturnsZero) {
    float from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    float hit[4] = {};

    EXPECT_EQ(CheckHit(nullptr, from, to, hit, 1, 0), 0);
}

TEST(GameUtilAsm, CheckHitEmptyCollisionReturnsMinusOne) {
    CollisionInfo info = MakeInfo(nullptr, 0);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 1, 0), -1);
}

TEST(GameUtilAsm, CheckHitsReturnsEveryCrossedPolygonInArrayOrder) {
    CCPoly        polys[3] = {Floor(0.0f), Floor(5.0f), Floor(20.0f)};
    CollisionInfo info = MakeInfo(polys, 3);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    int           hit_polys[8] = {};
    float         hit_points[8][4] = {};

    EXPECT_EQ(CheckHits(&info, from, to, 8, hit_polys, hit_points, 0, 0), 2);
    EXPECT_EQ(hit_polys[0], 0);
    EXPECT_EQ(hit_polys[1], 1);
    EXPECT_FLOAT_EQ(hit_points[0][3], 10.0f);
    EXPECT_FLOAT_EQ(hit_points[1][3], 5.0f);
}

TEST(GameUtilAsm, CheckHitsPositiveSortOrdersByDistanceFromOrigin) {
    CCPoly        polys[3] = {Floor(0.0f), Floor(5.0f), Floor(20.0f)};
    CollisionInfo info = MakeInfo(polys, 3);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    int           hit_polys[8] = {};
    float         hit_points[8][4] = {};

    EXPECT_EQ(CheckHits(&info, from, to, 8, hit_polys, hit_points, 1, 0), 2);
    EXPECT_EQ(hit_polys[0], 1);
    EXPECT_EQ(hit_polys[1], 0);
    EXPECT_FLOAT_EQ(hit_points[0][3], 5.0f);
    EXPECT_FLOAT_EQ(hit_points[1][3], 10.0f);
}

TEST(GameUtilAsm, CheckHitsNegativeSortAlsoSortsAscending) {
    CCPoly        polys[3] = {Floor(0.0f), Floor(5.0f), Floor(20.0f)};
    CollisionInfo info = MakeInfo(polys, 3);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    int           hit_polys[8] = {};
    float         hit_points[8][4] = {};

    EXPECT_EQ(CheckHits(&info, from, to, 8, hit_polys, hit_points, -1, 0), 2);
    EXPECT_EQ(hit_polys[0], 1);
    EXPECT_EQ(hit_polys[1], 0);
    EXPECT_FLOAT_EQ(hit_points[0][3], 5.0f);
    EXPECT_FLOAT_EQ(hit_points[1][3], 10.0f);
}

TEST(GameUtilAsm, CheckHitsStopsAtMaxHits) {
    CCPoly        polys[2] = {Floor(0.0f), Floor(5.0f)};
    CollisionInfo info = MakeInfo(polys, 2);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    int           hit_polys[8] = {};
    float         hit_points[8][4] = {};

    EXPECT_EQ(CheckHits(&info, from, to, 1, hit_polys, hit_points, 0, 0), 1);
    EXPECT_EQ(hit_polys[0], 0);
}

TEST(GameUtilAsm, TestVUnewZeroMatrixLeavesAccumulatorUnchanged) {
    float matrix[4][4] = {};
    float vertex[4] = {1.0f, 2.0f, 3.0f, 1.0f};
    float weight[4] = {0.5f, 0.0f, 0.0f, 0.0f};
    float accum[4] = {10.0f, 20.0f, 30.0f, 1.0f};
    float out[4] = {};

    testVUnew(matrix, vertex, weight, accum, out);

    EXPECT_FLOAT_EQ(accum[0], 10.0f);
    EXPECT_FLOAT_EQ(accum[1], 20.0f);
    EXPECT_FLOAT_EQ(accum[2], 30.0f);
    EXPECT_FLOAT_EQ(accum[3], 1.0f);
    EXPECT_FLOAT_EQ(out[0], 10.0f);
    EXPECT_FLOAT_EQ(out[1], 20.0f);
    EXPECT_FLOAT_EQ(out[2], 30.0f);
    EXPECT_FLOAT_EQ(out[3], 1.0f);
}

TEST(GameUtilAsm, CheckHitReturnsMinusOneForSegmentLyingInPlane) {
    CCPoly        polys[1] = {Floor(0.0f)};
    CollisionInfo info = MakeInfo(polys, 1);
    float         from[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float         to[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 1, 0), -1);
}

TEST(GameUtilAsm, CheckHitZeroLengthSegmentAboveFloorReturnsMinusOne) {
    CCPoly        polys[1] = {Floor(0.0f)};
    CollisionInfo info = MakeInfo(polys, 1);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         hit[4] = {};

    EXPECT_EQ(CheckHit(&info, from, to, hit, 1, 0), -1);
}

TEST(GameUtilAsm, CheckHitsEmptyPolygonListReturnsZeroForSort) {
    CCPoly        polys[1] = {Floor(0.0f)};
    CollisionInfo info = MakeInfo(polys, 0);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    int           hit_polys[8] = {};
    float         hit_points[8][4] = {};

    EXPECT_EQ(CheckHits(&info, from, to, 8, hit_polys, hit_points, 1, 0), 0);
}

TEST(GameUtilAsm, CheckHitsZeroMaxHitsReturnsZero) {
    CCPoly        polys[2] = {Floor(0.0f), Floor(5.0f)};
    CollisionInfo info = MakeInfo(polys, 2);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    int           hit_polys[8] = {};
    float         hit_points[8][4] = {};

    EXPECT_EQ(CheckHits(&info, from, to, 0, hit_polys, hit_points, 0, 0), 0);
}

TEST(GameUtilAsm, CheckHitsSortKeepsArrayOrderForTiedDistances) {
    CCPoly        polys[3] = {Floor(0.0f), Floor(5.0f), Floor(0.0f)};
    CollisionInfo info = MakeInfo(polys, 3);
    float         from[4] = {0.0f, 10.0f, 0.0f, 1.0f};
    float         to[4] = {0.0f, -10.0f, 0.0f, 1.0f};
    int           hit_polys[8] = {};
    float         hit_points[8][4] = {};

    EXPECT_EQ(CheckHits(&info, from, to, 8, hit_polys, hit_points, 1, 0), 3);
    EXPECT_EQ(hit_polys[0], 1);
    EXPECT_EQ(hit_polys[1], 0);
    EXPECT_EQ(hit_polys[2], 2);
    EXPECT_FLOAT_EQ(hit_points[0][3], 5.0f);
    EXPECT_FLOAT_EQ(hit_points[1][3], 10.0f);
    EXPECT_FLOAT_EQ(hit_points[2][3], 10.0f);
}

TEST(GameUtilAsm, MotionProc2ZeroKeyListReturnsNextAndWritesNothing) {
    float saved[4] = {7.0f, 8.0f, 9.0f, 10.0f};
    def_vrtx[0][0] = saved[0];
    def_vrtx[0][1] = saved[1];
    def_vrtx[0][2] = saved[2];
    def_vrtx[0][3] = saved[3];

    Mot_List next = {};
    Mot_List list = {};
    list.key_count = 0;
    list.next = &next;
    tagMOTION_TYPE motion = {};
    motion.skin_list = &list;

    EXPECT_EQ(MotionProc2(nullptr, &motion, nullptr, &list), &next);
    EXPECT_FLOAT_EQ(def_vrtx[0][0], saved[0]);
    EXPECT_FLOAT_EQ(def_vrtx[0][1], saved[1]);
    EXPECT_FLOAT_EQ(def_vrtx[0][2], saved[2]);
    EXPECT_FLOAT_EQ(def_vrtx[0][3], saved[3]);
}
