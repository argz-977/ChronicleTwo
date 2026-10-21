#include "common.h"

#include <math.h>

#include "actionchara.hpp"
#include "character.hpp"
#include "dng_hud.hpp"
#include "dng_main.hpp"
#include "dng_target_port.hpp"
#include "gfx/gfx.hpp"
#include "maintex.hpp"
#include "mg_drawprim.hpp"
#include "mglib.hpp"
#include "monster.hpp"
#include "prespr.hpp"
#include "scenesnd.hpp"
#include "userdata.hpp"

PORT_OVERRIDE void CWarningGage2::Draw() {
    if (time < 20 || layout == WARNING_GAGE_LAYOUT_NONE) {
        return;
    }

    struct WarningIcon {
        int x, y, width, height, u, v;
    };

    constexpr WarningIcon human_label[] = {
        {.x = 183, .y = 41, .width = 68, .height = 20, .u = 316, .v = 236},
        {.x = 274, .y = 64, .width = 68, .height = 20, .u = 316, .v = 236},
        {.x = 411, .y = 73, .width = 68, .height = 20, .u = 316, .v = 236}
    };
    constexpr WarningIcon human_icon[] = {
        {.x = 170, .y = 16, .width = 28, .height = 36, .u = 210, .v = 226},
        {.x = 302, .y = 15, .width = 26, .height = 54, .u = 290, .v = 202},
        {.x = 439, .y = 56, .width = 26, .height = 22, .u = 238, .v = 234}
    };
    constexpr WarningIcon robot_label[] = {
        {.x = 146, .y = 38, .width = 68, .height = 20, .u = 316, .v = 236},
        {.x = 320, .y = 33, .width = 68, .height = 20, .u = 316, .v = 236}
    };
    constexpr WarningIcon robot_icon[] = {
        {.x = 133, .y = 13, .width = 28, .height = 36, .u = 210, .v = 226},
        {.x = 348, .y = 16, .width = 26, .height = 22, .u = 238, .v = 234}
    };
    const bool human = layout == WARNING_GAGE_LAYOUT_MAIN;
    if (!human && layout != WARNING_GAGE_LAYOUT_ROBO) {
        return;
    }
    for (int i = 0; i < (human ? 3 : 2); ++i) {
        if (!warning[i] || (!human && i == 0 && rate[i] == 0.0f)) {
            continue;
        }
        auto label = human ? human_label[i] : robot_label[i];
        auto icon = human ? human_icon[i] : robot_icon[i];
        if (i != 0 && rate[i] == 0.0f) {
            label.v = 216;
            icon.u = human && i == 1 ? 264 : 238;
            icon.v = human && i == 1 ? 202 : 212;
        }
        // Warnings are emitted after DrawStatusBord, so they need the same panel's anchor here.
        gfx::UiAnchorScope anchor(human ? gfx::UiAnchor::Side(i == 0 ? -1 : 1, -1, true) : gfx::UiAnchor::Side(-1, -1, true));
        CPreSprite         sprite;
        sprite.Initialize(nullptr, nullptr);
        sprite.Preset2D();
        sprite.Begin(MG_PRIM_SPRITE);
        sprite.Texture(TEX_SystenFrame);
        sprite.Color(0x80, 0x80, 0x80, 0x80);
        sprite.SetIRect(label.x, label.y, label.width, label.height, label.u, label.v);
        sprite.SetIRect(icon.x, icon.y, icon.width, icon.height, icon.u, icon.v);
        sprite.End();
    }
}

// Retail's definition returns void against the fixed-up declaration (scripts/port/header_fixups.py).
PORT_OVERRIDE int CLockOnModel::Draw() {
    float target_pos[4];
    int   top_left[4];
    int   bottom_right[4];

    auto *player = static_cast<CActionChara *>(scene->GetCharacter(0));

    if (player == nullptr) {
        mgPortHideTarget(this);
        return 0;
    }

    name = nullptr;

    if (player->target_no == -1) {
        mgPortHideTarget(this);
        return 0;
    }

    auto *target = static_cast<CActiveMonster *>(scene->GetCharacter(player->target_no));

    if (target == nullptr) {
        mgPortHideTarget(this);
        return 0;
    }

    float size = NAN;
    float height = target->body_height;

    if (!(target->target_dist <= target->clip_dist)) {
        mgPortHideTarget(this);
        return 0;
    }

    if (target->alpha <= 0.0f) {
        mgPortHideTarget(this);
        return 0;
    }

    CHARA_ENTRY_OBJECT *entry = target->GetEntryObjectPos(CHARA_ENTRY_GROUP_TARGET, 0, target_pos);

    if (entry == nullptr) {
        mgPortHideTarget(this);
        return 0;
    }

    size = entry->size;
    sceVu0CopyVector(pos, target_pos);

    if (!(height <= 85.0f)) {
        height = 85.0f;
    }

    pos[1] += height;
    name = target->tbl->name;
    int monster_id = -1;

    if (DngUserData->active_chr_no == USER_CHARA_MONSTER) {
        monster_id = DngUserData->monster_id;
    }

    if (target->monster_id == monster_id) {
        int message = target->message_no;

        if (message >= 0) {
            message += 5000;
        }

        message_no = message;
    } else {
        message_no = -1;
    }

    mgPortTrackTarget(this, target, player->target_no, player->lock_on != 0);
    if (player->lock_on == 0) {
        target_pos[1] += 5.0f + 10.0f * size;
        SetRotation(0.0f, angle, 0.0f);
        SetPosition(target_pos);
        CObjectFrame::DrawDirect();
        return 0;
    }

    gfx::Interp2DScope interpolation(mgPortTargetBrackets(this));
    CPreSprite         prim;
    prim.Initialize(nullptr, nullptr);
    prim.Preset2D();
    prim.Coord(1);
    prim.Begin(MG_PRIM_SPRITE);
    prim.Texture(TEX_SystenFrame);
    prim.Color(0x80, 0x80, 0x80, 0x80);

    if (mgTransWorldPrim3DSprite(top_left, bottom_right, target_pos, 20.0f * size, 20.0f * size, 0) != 0) {
        bottom_right[0] -= 0x120;
        bottom_right[1] -= 0x120;
        prim.TextureCrd(0xC0, 0x16);
        prim.Vertex4(top_left[0], top_left[1], 0);
        prim.TextureCrd(0xD2, 0x28);
        prim.Vertex4(top_left[0] + 0x120, top_left[1] + 0x120, 0);
        prim.TextureCrd(0xD2, 0x16);
        prim.Vertex4(bottom_right[0], top_left[1], 0);
        prim.TextureCrd(0xE4, 0x28);
        prim.Vertex4(bottom_right[0] + 0x120, top_left[1] + 0x120, 0);
        prim.TextureCrd(0xC0, 0x26);
        prim.Vertex4(top_left[0], bottom_right[1], 0);
        prim.TextureCrd(0xD2, 0x3A);
        prim.Vertex4(top_left[0] + 0x120, bottom_right[1] + 0x120, 0);
        prim.TextureCrd(0xD2, 0x28);
        prim.Vertex4(bottom_right[0], bottom_right[1], 0);
        prim.TextureCrd(0xE4, 0x3A);
        prim.Vertex4(bottom_right[0] + 0x120, bottom_right[1] + 0x120, 0);
    }

    prim.End();

    return 0;
}
