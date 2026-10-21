#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "dataread.hpp"
#include "dataread_port.hpp"
#include "mainloop.hpp"
#include "menucapt.hpp"
#include "menumain.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "platform/paths.hpp"
#include "scene.hpp"

TEST(MenuChapter, LoadsLocalizedChapterTextureAndFallbackFromTextureNamespace) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      ("dc2_chapter_textures_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto previous_root = PathsDataRoot();
    const int  previous_language = LanguageCode;
    fs::create_directories(root / "data/textures/menu/1");
    const auto document = [](const char *probe) {
        nlohmann::json entries = nlohmann::json::array();
        for (const char *name : {"chapbg", "chaplogo", probe}) {
            entries.push_back({
                {"name",     name                                         },
                {"pictures", {{{"canvas", {{"width", 2}, {"height", 2}}}}}}
            });
        }
        return nlohmann::json{
            {"schema_version", 3      },
            {"groups",         {0}    },
            {"entries",        entries}
        };
    };
    std::ofstream(root / "data/textures/menu/1/chap7.json") << document("chapter_seven_probe").dump();
    std::ofstream(root / "data/textures/menu/1/chap0.json") << document("chapter_zero_probe").dump();
    DataReadIndex(root / "data");
    LanguageCode = LANG_ENGLISH;

    alignas(16) static std::array<u_long128, 1024> texture_storage{};
    mgCMemory                                      texture_memory;
    texture_memory.SetHeapMem(texture_storage.data(), texture_storage.size());
    texture_memory.StartStackMode(MG_STACK_MODE_FIT, texture_storage.size());
    mgTexManager.SetTableBuffer(16, 4, &texture_memory);
    mgTexManager.Initialize(0x1000, -1);

    auto scene = std::unique_ptr<CScene, decltype(&std::free)>(static_cast<CScene *>(std::calloc(1, sizeof(CScene))), &std::free);
    ASSERT_TRUE(scene);
    auto *saved_scene = MenuMainScene;
    MenuMainScene = scene.get();
    std::vector<u_long128> storage(0x4000);
    mgCMemory              stack;
    stack.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
    int blocks[] = {1, 2};
    MenuChapterInit(&stack, blocks, 0, 7);
    char background[] = "chapbg";
    char logo[] = "chaplogo";
    char chapter_seven[] = "chapter_seven_probe";
    EXPECT_NE(mgTexManager.GetTexture(background, blocks[0]), nullptr);
    EXPECT_NE(mgTexManager.GetTexture(logo, blocks[0]), nullptr);
    EXPECT_NE(mgTexManager.GetTexture(chapter_seven, blocks[0]), nullptr);

    mgTexManager.DeleteBlock(blocks[0]);
    MenuChapterInit(&stack, blocks, 0, 9);
    char chapter_zero[] = "chapter_zero_probe";
    EXPECT_NE(mgTexManager.GetTexture(chapter_zero, blocks[0]), nullptr);

    mgTexManager.Initialize(0x1000, -1);
    MenuMainScene = saved_scene;
    LanguageCode = previous_language;
    DataReadIndex(previous_root);
    std::error_code error;
    fs::remove_all(root, error);
}

TEST(MenuChapter, MissingNarrationTimesOutAndReopeningRestartsFade) {
    const auto root = std::filesystem::temp_directory_path() / "dc2_chapter_test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "manifest.json") << R"({"schema_version":1,"files":{}})";
    PathsConfigure({.app_dir = "dc2-chapter-test", .env_data = "DC2_CHAPTER_TEST_DATA", .env_save = "DC2_CHAPTER_TEST_SAVE"});
    PathsSetDataRoot(root);
    InitCDFile();
    auto scene = std::unique_ptr<CScene, decltype(&std::free)>(
        static_cast<CScene *>(std::calloc(1, sizeof(CScene))), &std::free);
    ASSERT_TRUE(scene);
    auto *saved_scene = MenuMainScene;
    MenuMainScene = scene.get();
    std::vector<u_long128> storage(0x4000);
    mgCMemory              stack;
    stack.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
    int blocks[] = {1, 2};
    MenuChapterInit(&stack, blocks, 0, 0);
    EXPECT_EQ(MenuMainScene->fade.end, 0);
    int finished = 0;
    for (int frame = 0; frame < 2500 && !finished; ++frame) {
        MenuMainScene->fade.FadeStep();
        finished = MenuChapterKey();
    }
    EXPECT_EQ(finished, 1);
    MenuChapterInit(&stack, blocks, 0, 7);
    EXPECT_EQ(MenuChapterKey(), 0);
    EXPECT_EQ(MenuMainScene->fade.end, 0);
    EXPECT_FLOAT_EQ(MenuMainScene->fade.alpha, 128.0f);
    MenuMainScene = saved_scene;
    std::filesystem::remove_all(root);
}

TEST(GameplayTimingChapter, NarrationFallbackAndHoldUseNtscFrames) {
    const auto root = std::filesystem::temp_directory_path() / "dc2_chapter_timing_test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "manifest.json") << R"({"schema_version":1,"files":{}})";
    PathsConfigure({.app_dir = "dc2-chapter-timing-test", .env_data = "DC2_CHAPTER_TEST_DATA", .env_save = "DC2_CHAPTER_TEST_SAVE"});
    PathsSetDataRoot(root);
    InitCDFile();
    auto scene = std::unique_ptr<CScene, decltype(&std::free)>(
        static_cast<CScene *>(std::calloc(1, sizeof(CScene))), &std::free);
    ASSERT_TRUE(scene);
    auto *saved_scene = MenuMainScene;
    MenuMainScene = scene.get();
    std::vector<u_long128> storage(0x4000);
    mgCMemory              stack;
    stack.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
    int blocks[] = {1, 2};
    MenuChapterInit(&stack, blocks, 0, 0);
    scene->fade.end = 1;
    // ceil(128 / 3) logo steps enter SHOW; none of these count as narration.
    for (int frame = 0; frame < 43; ++frame) {
        EXPECT_EQ(MenuChapterKey(), 0);
    }
    const int fallback = 1800;
    const int hold = 406;
    for (int frame = 0; frame < fallback; ++frame) {
        EXPECT_EQ(MenuChapterKey(), 0);
        EXPECT_EQ(scene->fade.mode, FADE_MODE_IN);
    }
    for (int frame = 1; frame < hold; ++frame) {
        EXPECT_EQ(MenuChapterKey(), 0);
        EXPECT_EQ(scene->fade.mode, FADE_MODE_IN);
    }
    EXPECT_EQ(MenuChapterKey(), 0);
    EXPECT_EQ(scene->fade.mode, FADE_MODE_OUT);
    scene->fade.end = 1;
    EXPECT_EQ(MenuChapterKey(), 1);
    MenuMainScene = saved_scene;
    std::filesystem::remove_all(root);
}
