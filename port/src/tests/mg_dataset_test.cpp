#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "mg_dataset.hpp"

TEST(MgDataset, CopyFrameTableDoesNotOverlapFrames) {
    constexpr int                  count = 64;
    std::array<mgCFrame, count>    frames;
    std::array<mgCFrame *, count>  table{};
    std::array<std::string, count> names;
    for (int i = 0; i < count; ++i) {
        names[i] = "frame" + std::to_string(i);
        frames[i].SetName(names[i].data());
        frames[i].SetPosition(static_cast<float>(i), 2.0f, 3.0f);
        table[i] = &frames[i];
    }
    frames[0].frame_num = count;
    frames[0].frame_list = table.data();

    static u_long128 storage[4096];
    mgCMemory        memory;
    memory.stSetBuffer(storage, 4096);
    mgCFrame *copy = mgCopyFrame(frames.data(), &memory, 0);
    ASSERT_NE(copy, nullptr);
    ASSERT_EQ(copy->frame_num, count);
    ASSERT_NE(copy->frame_list, nullptr);
    ASSERT_LE(reinterpret_cast<std::uintptr_t>(copy->frame_list + count),
              reinterpret_cast<std::uintptr_t>(copy));
    for (int i = 0; i < count; ++i) {
        mgCFrame *node = copy->frame_list[i];
        ASSERT_EQ(node, copy + i);
        EXPECT_STREQ(node->name, names[i].c_str());
        EXPECT_FLOAT_EQ(node->position[0], static_cast<float>(i));
        EXPECT_EQ(node->parent, nullptr);
        EXPECT_EQ(node->child, nullptr);
        EXPECT_EQ(node->brother, nullptr);
        EXPECT_EQ(frames[i].name, names[i].data());
    }
}

TEST(MgDataset, CopyFrameRebuildsHierarchyInCopiedFrames) {
    constexpr int                  count = 68;
    std::array<mgCFrame, count>    frames;
    std::array<mgCFrame *, count>  table{};
    std::array<std::string, count> names;
    for (int i = 0; i < count; ++i) {
        names[i] = "bone" + std::to_string(i);
        frames[i].SetName(names[i].data());
        table[i] = &frames[i];
        if (i > 0) {
            frames[i].SetParent(&frames[i - 1]);
        }
    }
    frames[0].frame_num = count;
    frames[0].frame_list = table.data();

    static u_long128 storage[4096];
    mgCMemory        memory;
    memory.stSetBuffer(storage, 4096);
    mgCFrame *copy = mgCopyFrame(frames.data(), &memory, 0);
    ASSERT_NE(copy, nullptr);
    ASSERT_EQ(copy->frame_num, count);
    ASSERT_EQ(copy->parent, nullptr);
    for (int i = 0; i < count; ++i) {
        ASSERT_EQ(copy->frame_list[i], copy + i);
        EXPECT_EQ(copy[i].parent, i > 0 ? copy + i - 1 : nullptr);
        EXPECT_EQ(copy[i].child, i + 1 < count ? copy + i + 1 : nullptr);
        EXPECT_EQ(copy[i].brother, nullptr);
        EXPECT_EQ(frames[i].parent, i > 0 ? &frames[i - 1] : nullptr);
    }
    EXPECT_EQ(copy->SearchFrame(names.back().data()), copy + count - 1);
}
