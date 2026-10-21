#include "common.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "charasetup.hpp"
#include "dataread.hpp"
#include "dataread_port.hpp"
#include "gfx/gfx.hpp"
#include "menu_character_assets.hpp"
#include "menuaqua.hpp"
#include "menumain.hpp"
#include "mglib.hpp"
#include "platform/paths.hpp"
#include "userdata.hpp"

namespace {
/**
 * Restores menu globals after exercising independent native workspace and texture-directory lifetimes.
 */
struct MenuGlobals {
    CMenuPosDataManage *positions = MenuPosData;            /**< Previous menu layout manager. */
    CMenuKeyFunc       *common = MenuCommonInfo;            /**< Previous common menu state. */
    u_int              *pack = MenuArg.pack;                /**< Previous actual menu directory marker. */
    mgCMemory           workspace = MenuMainTextureReadBuf; /**< Previous menu work arena. */

    ~MenuGlobals() {
        MenuPosData = positions;
        MenuCommonInfo = common;
        MenuArg.pack = pack;
        MenuMainTextureReadBuf = workspace;
    }
};
} // namespace

TEST(MenuWorkspace, CharacterSoundPathsUseConvertedAssetsAndHandleMissingRidepodArm) {
    auto user = std::make_unique<CUserDataManager>();
    char path[0x60]{};
    user->chara_data[USER_CHARA_MAX].equip[CHARA_EQUIP_SUB].item_no = 0x16;
    GetCharacterSnd(user.get(), USER_CHARA_MAX, path);
    EXPECT_STREQ(path, "snd2/chara/CH_000.sound.json");
    user->chara_data[USER_CHARA_MAX].equip[CHARA_EQUIP_SUB].item_no = 0x28;
    GetCharacterSnd(user.get(), USER_CHARA_MAX, path);
    EXPECT_STREQ(path, "snd2/chara/CH_018.sound.json");
    user->chara_data[USER_CHARA_MAX].equip[CHARA_EQUIP_SUB].item_no = -1;
    GetCharacterSnd(user.get(), USER_CHARA_MAX, path);
    EXPECT_STREQ(path, "snd2/chara/CH_000.sound.json");
    GetCharacterSnd(user.get(), USER_CHARA_MONICA, path);
    EXPECT_STREQ(path, "snd2/chara/CH_020.sound.json");

    user->robo_data.parts[ROBO_PART_ARM].item_no = -1;
    std::strcpy(path, "stale");
    GetCharacterSnd(user.get(), USER_CHARA_ROBO, path);
    EXPECT_STREQ(path, "");
    std::strcpy(path, "stale");
    GetCharacterSnd(user.get(), USER_CHARA_MONSTER, path);
    EXPECT_STREQ(path, "");
    std::strcpy(path, "stale");
    GetCharacterSnd(nullptr, USER_CHARA_MAX, path);
    EXPECT_STREQ(path, "");
    GetCharacterSnd(user.get(), USER_CHARA_MAX, nullptr);
}

TEST(MenuWorkspace, CharacterChangeFindsItemTextureWithOrWithoutSoundRead) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("menu_item_read_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto previous_root = PathsDataRoot();
    char       previous_directory[0x6C]{};
    GetCurrentDir(previous_directory);
    const auto data = root / "data";
    std::filesystem::create_directories(data / "snd2/chara");
    std::filesystem::create_directories(data / "textures/img");
    std::ofstream(data / "snd2/chara/CH_000.sound.json") << "sound";
    std::ofstream(data / "textures/img/allitem.json") << "texture";
    DataReadIndex(data);
    SetCurrentDir(nullptr);

    alignas(16) std::array<u_long128, 256> sound{};
    alignas(16) std::array<u_long128, 256> item{};
    char                                   sound_path[] = "snd2/chara/CH_000.sound.json";
    char                                   item_path[] = "textures/img/allitem.json";
    InitReadBG();
    ASSERT_TRUE(LoadFileBG(sound_path, sound.data(), nullptr));
    ASSERT_TRUE(LoadFileBG(item_path, item.data(), nullptr));
    EXPECT_EQ(PortMainCharaItemReadBuffer(), reinterpret_cast<unsigned char *>(item.data()));

    InitReadBG();
    char missing_sound[] = "snd2/chara/missing.sound.json";
    EXPECT_FALSE(LoadFileBG(missing_sound, sound.data(), nullptr));
    ASSERT_TRUE(LoadFileBG(item_path, item.data(), nullptr));
    EXPECT_EQ(PortMainCharaItemReadBuffer(), reinterpret_cast<unsigned char *>(item.data()));

    InitReadBG();
    SetCurrentDir(previous_directory);
    DataReadIndex(previous_root);
    std::filesystem::remove_all(root);
}

TEST(MenuWorkspace, DirectoryMarkerDoesNotLimitAquariumStorageOrTextureReentry) {
    const auto root = std::filesystem::temp_directory_path() / ("menu_workspace_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto previous_root = PathsDataRoot();
    std::filesystem::create_directories(root / "data");
    const auto directory = root / "data/menu/native.pac";
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "pack.json") << R"({"schema_version":1,"kind":"asset_directory","texture_members":[{"name":"edmenu.json","path":"textures/menu/edmenu.json"},{"name":"allitem.json","path":"textures/menu/allitem.json"}]})";
    const auto texture_directory = root / "data/textures/menu";
    std::filesystem::create_directories(texture_directory);
    std::ofstream(texture_directory / "edmenu.json") << R"({"schema_version":3,"groups":[0],"entries":[{"name":"menuwork2","pictures":[{"levels":["pixel.png"]}]}]})";
    std::ofstream(texture_directory / "allitem.json") << R"({"schema_version":3,"groups":[0],"entries":[]})";
    constexpr std::array<unsigned char, 4> pixel{32, 64, 128, 255};
    ASSERT_TRUE(gfx::WritePng(texture_directory / "pixel.png", pixel.data(), 1, 1));
    DataReadIndex(root / "data");
    SetCurrentDir(nullptr);
    alignas(16) std::array<u_long128, 128> marker{};
    char                                   path[] = "menu/native.pac";
    int                                    marker_size = 0;
    ASSERT_TRUE(LoadFile2(path, marker.data(), &marker_size, LOAD_FILE_READ));
    ASSERT_LT(marker_size, static_cast<int>(sizeof(marker)));
    const auto             original_marker = marker;
    std::vector<u_long128> texture_storage(4096);
    std::vector<u_long128> palette_storage(1024);
    mgCMemory              textures;
    mgCMemory              palettes;
    textures.stSetBuffer(texture_storage.data(), static_cast<int>(texture_storage.size()));
    palettes.stSetBuffer(palette_storage.data(), static_cast<int>(palette_storage.size()));
    mgTexManager.SetTableBuffer(64, 8, &textures);
    mgTexManager.Initialize(0x1000, -1);
    MenuGlobals        restore;
    CMenuPosDataManage positions{};
    positions.Initialize();
    CMenuKeyFunc common{};
    common.tex_block[1] = 1;
    MenuPosData = &positions;
    MenuCommonInfo = &common;
    MenuArg.pack = reinterpret_cast<u_int *>(marker.data());
    MenuCommonBaseDataEnter(&palettes, MenuArg.pack, marker_size, 1);
    char texture_name[] = "menuwork2";
    ASSERT_NE(MenuMainTextureReadBuf.stack, marker.data());
    EXPECT_GE(MenuMainTextureReadBuf.stack_size, 260000);
    ASSERT_NE(mgTexManager.GetTexture(texture_name, 1), nullptr);

    // Aquarium initialization carves these sound, message, interior and fish-model extents.
    for (int blocks : {0x80, 0x4650, 0x1e00}) {
        ASSERT_NE(MenuMainTextureReadBuf.Alloc(blocks), nullptr);
    }
    for (int slot = 0; slot < AQUA_FISH_SLOT_NUM; ++slot) {
        ASSERT_NE(MenuMainTextureReadBuf.Alloc(sizeof(CAquaFishEff) / 16 + 2), nullptr);
        ASSERT_NE(MenuMainTextureReadBuf.Alloc(sizeof(CBubble) / 16 + 2), nullptr);
        ASSERT_NE(MenuMainTextureReadBuf.Alloc(0x319c), nullptr);
    }
    std::memset(MenuMainTextureReadBuf.stack, 0xa5, static_cast<std::size_t>(MenuMainTextureReadBuf.stack_used) * sizeof(u_long128));
    MenuBaseTextureReEnter();
    EXPECT_NE(mgTexManager.GetTexture(texture_name, 1), nullptr);
    EXPECT_EQ(positions.common_tex, mgTexManager.GetTexture(texture_name, 1));
    EXPECT_EQ(std::memcmp(marker.data(), original_marker.data(), sizeof(marker)), 0);
    EXPECT_EQ(DataReadBufferSize(marker.data()), static_cast<std::size_t>(marker_size));
    mgTexManager.Initialize(0x1000, -1);
    DataReadIndex(previous_root);
    std::filesystem::remove_all(root);
}
