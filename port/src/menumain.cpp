#include "common.h"

#include <string>
#include <vector>

#include "asset_directory.hpp"
#include "dataread_port.hpp"
#include "menu_world.hpp"
#include "menumain.hpp"
#include "mg_texture_port.hpp"
#include "mglib.hpp"
#include "stubs/menumain.hpp"
#include "texture_source.hpp"

namespace {
constexpr int kMenuWorkspaceQuadwords = 260000; /**< Capacity of the native menu's reusable work arena. */

/**
 * Physical common texture paths retained while workspace bytes are reused.
 */
std::vector<std::string> common_texture_paths;

/**
 * Owns menu workspace independently of the small serialized directory marker.
 */
std::vector<u_long128> &MenuWorkspace() {
    static std::vector<u_long128> workspace(kMenuWorkspaceQuadwords);
    return workspace;
}

/**
 * Retains physical common texture paths from the menu's logical PACK members.
 */
void RetainCommonTextures(const void *pack) {
    common_texture_paths.clear();
    for (const auto *name : {"edmenu.json", "allitem.json", "spectre.json"}) {
        if (const auto *texture = AssetDirectoryRead(pack, name, nullptr)) {
            if (const auto path = DataReadBufferPath(texture)) {
                common_texture_paths.push_back(*path);
            }
        }
    }
}

/**
 * Enters common texture documents from their retained physical paths.
 */
void EnterCommonTextures(int block) {
    for (const auto &path : common_texture_paths) {
        MgTextureEnterDocument(&mgTexManager, path, block, nullptr, nullptr);
    }
}
} // namespace

PORT_OVERRIDE void MenuCommonBaseDataEnter(mgCMemory *pallet_memory, u_int *pack, int /*unused*/, int block) {
    RetainCommonTextures(pack);
    auto &workspace = MenuWorkspace();
    DataReadRegisterBuffer(workspace.data(), 0);
    TextureSourceForget(workspace.data());
    MenuMainTextureReadBuf.stSetBuffer(workspace.data(), static_cast<int>(workspace.size()));
    EnterCommonTextures(block);
    MenuPosData->AttachCommonTexInfo();
    MenuItemIconTextureBlock = block;
    MenuPosData->MallocPallet(pallet_memory);
    MenuPosData->SearchTransPalletNo();
    MenuPosData->ResetTextureInfoAll();
}

PORT_OVERRIDE void MenuBaseTextureReEnter() {
    if (DataReadBufferSize(MenuMainTextureReadBuf.stack) != 0) {
        if (const auto directory = AssetDirectoryPath(MenuMainTextureReadBuf.stack)) {
            RetainCommonTextures(MenuMainTextureReadBuf.stack);
        }
    }
    const int block = MenuCommonInfo->tex_block[1];
    mgTexManager.DeleteBlock(block);
    MenuMainImageDataEnter(block);
    EnterCommonTextures(block);
    MenuPosData->AttachCommonTexInfo();
    MenuPosData->ResetTextureInfoAll();
}

PORT_OVERRIDE int MenuMainInit(MENU_INIT_ARG *arg) {
    menuworld::MenuOpening(arg);
    return PortRetailMenuMainInit(arg);
}

PORT_OVERRIDE int MenuMainExit() {
    menuworld::MenuClosed();
    return PortRetailMenuMainExit();
}

PORT_OVERRIDE void MenuWorldTrans() {
    if (!menuworld::ViewMenuCharacter()) {
        PortRetailMenuWorldTrans();
    }
}
