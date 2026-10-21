#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

#include "mg_drawprim.hpp"
#include "mg_memory.hpp"
#include "mg_tanime.hpp"
#include "mg_texture.hpp"
#include "scene.hpp"
#include "scenesnd.hpp"

namespace {

constexpr int kStorageQuads = 1024;
constexpr int kGroupCount = 40;

std::vector<u_long128> Storage(int quadwords, uint8_t fill) {
    std::vector<u_long128> storage(quadwords);
    std::memset(storage.data(), fill, storage.size() * sizeof(u_long128));
    return storage;
}

struct ScenePtrDeleter {
    void operator()(CScene *scene) const {
        std::free(scene);
    }
};

// CScene's constructor runs the whole scene init; the stack table is the only state under test.
using SceneHandle = std::unique_ptr<CScene, ScenePtrDeleter>;

SceneHandle ZeroedScene() {
    return SceneHandle(static_cast<CScene *>(std::calloc(1, sizeof(CScene))));
}

} // namespace

TEST(StrideFix, DrawManagerTablesHoldPointers) {
    // Filled before stSetBuffer so stale bytes show up in every table a 4-byte stride would miss.
    std::vector<u_long128> storage = Storage(kStorageQuads, 0xFF);
    mgCMemory              memory;
    memory.stSetBuffer(storage.data(), kStorageQuads);

    mgCTextureManager texture_manager;
    texture_manager.block_max = kGroupCount;

    mgCDrawManager manager;
    manager.texture_manager = &texture_manager;
    // Retail ctor leaves render_info unset; the game's instance is zero-initialised, so match that default.
    manager.render_info = nullptr;
    manager.SetSortTable(2);
    manager.BeginDraw(&memory, nullptr);

    ASSERT_EQ(manager.group_num, kGroupCount);
    for (int i = 0; i < kGroupCount; i++) {
        EXPECT_EQ(manager.packet_list[i], nullptr) << "group " << i;
        EXPECT_EQ(manager.packet_num[i], 0) << "group " << i;
    }
    for (int j = 0; j < manager.sort_num; j++) {
        EXPECT_EQ(manager.sort_table[j], nullptr) << "bucket " << j;
    }

    u_long128 commons[4] = {};
    u_long128 packets[4] = {};
    // Groups past 8 are the ones a 4-byte stride would collide with.
    const int groups[] = {0, 3, 9, 12, 39, 12};
    for (std::size_t i = 0; i < std::size(groups); i++) {
        manager.AddPacket(groups[i], &commons[i % 4], &packets[i % 4], 0);
    }

    std::vector<mgSORT_PACKET *> chain;
    for (mgSORT_PACKET *node = manager.sort_table[0]; node != nullptr; node = node->next) {
        chain.push_back(node);
    }
    ASSERT_EQ(chain.size(), std::size(groups));

    manager.PreEndDraw();

    for (int group = 0; group < kGroupCount; group++) {
        std::vector<mgSORT_PACKET *> expected;
        for (mgSORT_PACKET *node : chain) {
            if (node->group == group) {
                expected.push_back(node);
            }
        }

        ASSERT_EQ(manager.packet_num[group], static_cast<int>(expected.size())) << "group " << group;
        if (expected.empty()) {
            continue;
        }

        ASSERT_NE(manager.packet_list[group], nullptr) << "group " << group;
        for (std::size_t k = 0; k < expected.size(); k++) {
            EXPECT_EQ(manager.packet_list[group][k], expected[k]) << "group " << group << " slot " << k;
        }
    }
}

TEST(StrideFix, ClearStackWalksPointerArray) {
    constexpr int kStackCount = 5;
    constexpr int kNullSlot = 3;
    constexpr int kStackQuads = 64;

    std::vector<std::vector<u_long128>> buffers;
    std::vector<mgCMemory>               memories(kStackCount);
    for (int i = 0; i < kStackCount; i++) {
        buffers.push_back(Storage(kStackQuads, 0));
        memories[i].stSetBuffer(buffers[i].data(), kStackQuads);
        memories[i].Alloc(3);
        memories[i].lock = 1;
    }

    SceneHandle scene = ZeroedScene();
    scene->stack_num = kStackCount;
    for (int i = 0; i < kStackCount; i++) {
        scene->stack[i] = &memories[i];
    }
    scene->stack[kNullSlot] = nullptr;

    const int size_at_index = memories[1].stack_size;
    scene->ClearStack(1);

    EXPECT_EQ(memories[0].stack_used, 3);
    EXPECT_EQ(memories[0].lock, 1);

    // The entry at the index keeps its buffer; only the entries after it release theirs.
    EXPECT_EQ(memories[1].stack_used, 0);
    EXPECT_EQ(memories[1].lock, 0);
    EXPECT_NE(memories[1].stack, nullptr);
    EXPECT_EQ(memories[1].stack_size, size_at_index);

    for (int i : {2, 4}) {
        EXPECT_EQ(memories[i].stack_used, 0) << "stack " << i;
        EXPECT_EQ(memories[i].lock, 0) << "stack " << i;
        EXPECT_EQ(memories[i].stack, nullptr) << "stack " << i;
        EXPECT_EQ(memories[i].stack_size, 0) << "stack " << i;
    }

    EXPECT_EQ(scene->stack[kNullSlot], nullptr);
}

TEST(StrideFix, TexAnimeGroupAppends) {
    constexpr int          kStackQuads = 256;
    std::vector<u_long128> storage = Storage(kStackQuads, 0);
    mgCMemory              stack;
    stack.stSetBuffer(storage.data(), kStackQuads);

    mgCTextureAnime         anime;
    CList<mgCTexAnimeData> *first = anime.NewTexAnimeGroupData(2, &stack);
    CList<mgCTexAnimeData> *second = anime.NewTexAnimeGroupData(2, &stack);

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(anime.list[2], first);
    EXPECT_EQ(anime.now[2], first);
    EXPECT_EQ(first->next, second);
    EXPECT_EQ(second->prev, first);
    EXPECT_EQ(second->next, nullptr);
    EXPECT_EQ(anime.list[0], nullptr);
    EXPECT_EQ(anime.list[1], nullptr);
}
