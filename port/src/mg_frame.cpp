#include "common.h"

#include <libvu0.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "camera_fixed.hpp"
#include "mg_drawenv.hpp"
#include "mg_frame.hpp"
#include "mg_math.hpp"
#include "mg_visual.hpp"
#include "mg_visual_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "stubs/mg_frame.hpp"

PORT_OVERRIDE void mgCObject::SetPosition(float *input) {
    // Position consumes xyz; the stored homogeneous lane is always one.
    // Some callers provide exactly three floats.
    if (position[0] != input[0] || position[1] != input[1] || position[2] != input[2]) {
        use_srt = 1;
        position[0] = input[0];
        position[1] = input[1];
        position[2] = input[2];
        position[3] = 1.0f;
        changed = 1;
    }
}

PORT_OVERRIDE void mgCFrame::GetWorldPosition0(float *out_position) {
    sceVu0FMATRIX world;
    GetLWMatrix(world);
    // Callers may store vectors at the natural alignment of host pointers.
    std::memcpy(out_position, world[3], sizeof(world[3]));
}

PORT_OVERRIDE int mgCFrame::Draw(unsigned int *packet) {
    mgRENDER_INFO *info = &mgRenderInfo;
    int            words = 0;
    sceVu0FMATRIX  world;

    if (attr == nullptr) {
        GetLWMatrixTopBottom(world);
    } else {
        if (attr->billboard != 0) {
            GetBBoardMatrix(attr->billboard, world, info);
        } else {
            GetLWMatrixTopBottom(world);
        }

        while (attr != nullptr && (attr->draw & MG_FRAME_DRAW_VISIBLE)) {
            if (visual == nullptr) {
                break;
            }

            if (!attr->no_cull && bound != nullptr) {
                sceVu0FVECTOR box_max;
                sceVu0FVECTOR box_min;
                test1(bound->corner, info->world_screen_rel, world, box_max, box_min);

                if (box_max[3] < info->clip_min[2]) {
                    break;
                }

                test2(box_max, box_min);

                const mgPortExtent extent = mgPortVisibleExtent();
                const float        centre_x = 0.5f * static_cast<float>(mgScreenWidth);
                const float        centre_y = 0.5f * static_cast<float>(mgScreenHeight);
                float              visible_max[4] = {extent.right - centre_x, extent.bottom - centre_y, info->screen_box_max[2],
                                                     info->screen_box_max[3]};
                float              visible_min[4] = {extent.left - centre_x, extent.top - centre_y, info->screen_box_min[2],
                                                     info->screen_box_min[3]};

                if (TownMouseFixedOwner()) {
                    const float radius = MouseFixedCullRadius(info->projection,
                                                              info->projection * std::fabs(info->aspect[1][1]),
                                                              std::max(std::fabs(visible_min[0]), std::fabs(visible_max[0])),
                                                              std::max(std::fabs(visible_min[1]), std::fabs(visible_max[1])));
                    visible_min[0] = visible_min[1] = -radius;
                    visible_max[0] = visible_max[1] = radius;
                }
                if (!mgClipBoxW(box_max, box_min, visible_max, visible_min)) {
                    break;
                }

                if (mgClipInBoxW(box_max, box_min, info->gs_box_max, info->gs_box_min)) {
                    info->clip = 0;
                    info->scissor = 0;
                } else {
                    info->clip = 1;

                    if (attr->program_mode & 2) {
                        info->scissor = attr->clip_enable != 0;
                    } else {
                        info->scissor = (attr->clip_enable != 0) | info->all_scissor;
                    }
                }
            }

            info->attr = attr;
            sceVu0CopyVector(info->object_color, attr->color);
            info->plight_hit = 0;

            if (info->plight_enable && attr->point_light && !attr->no_light && bound != nullptr) {
                sceVu0FMATRIX transposed;
                sceVu0TransposeMatrix(transposed, world);
                float scale = mgDistVector(transposed[0]);
                float y_scale = mgDistVector(transposed[1]);
                float z_scale = mgDistVector(transposed[2]);
                scale = scale > y_scale ? (scale > z_scale ? scale : z_scale)
                                        : (y_scale > z_scale ? y_scale : z_scale);
                float         radius = bound->radius * scale;
                sceVu0FVECTOR center;
                sceVu0CopyVector(center, bound->center);
                center[3] = 1.0f;
                sceVu0ApplyMatrix(center, world, center);
                mgLIGHT_INFO *light = info->GetpLightInfo();

                for (auto &i : light->point_light) {
                    if (!(i.power <= 0.0f)) {
                        float reach = radius + i.range;

                        if (!(reach <= mgDistVector(i.pos, center))) {
                            info->plight_hit = 1;
                            break;
                        }
                    }
                }
            }

            {
                mgVisualPortObjectScope object_scope(this);
                words += visual->Draw(packet, world, nullptr);
            }
            break;
        }

        if (attr->draw & MG_FRAME_DRAW_SKIP_CHILDREN) {
            return words;
        }
    }

    for (mgCFrame *node = child; node != nullptr; node = node->brother) {
        if (node->attr == nullptr || !(node->attr->draw & MG_FRAME_DRAW_SKIP_BY_PARENT)) {
            words += node->Draw(packet + words * 4);
        }
    }

    return words;
}
