#include "common.h"

#include <cmath>
#include <cstring>

#include "mg_frame.hpp"
#include "mg_math.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/model_assets.hpp"
#include "stubs/mg_frame.hpp"
#include "vu0_lanes.hpp"

namespace {

// The VU0 registers vf10-vf17 as test1 leaves them: the transformed corners that test2 divides.
float vu_corners[8][4];

mgCFrameAttr dmy_attr;

void MulMatrices(float (*out)[4], const float (*lhs)[4], const float (*rhs)[4]) {
    float product[4][4];

    for (int i = 0; i < 4; i++) {
        vu0::Transform(product[i], rhs, lhs[i]);
    }

    std::memcpy(out, product, sizeof(product));
}

void TransformCorners(float (*out)[4], const float (*corners)[4], const float (*m)[4]) {
    float result[8][4];

    for (int c = 0; c < 8; c++) {
        vu0::Transform(result[c], m, corners[c]);
    }

    std::memcpy(out, result, sizeof(result));
}

// Rows 0-2 are scaled column by column; the translation row is kept as it is.
void ScaleColumns(float (*out)[4], const float (*in)[4], const float *s) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            out[i][j] = in[i][j] * s[j];
        }
    }

    for (int j = 0; j < 4; j++) {
        out[3][j] = in[3][j];
    }
}

void DivideByW(float (*out)[4], const float (*in)[4]) {
    for (int c = 0; c < 8; c++) {
        float q = vu0::Div(1.0f, std::fabs(in[c][3]));
        out[c][0] = vu0::Sat(in[c][0] * q);
        out[c][1] = vu0::Sat(in[c][1] * q);
        out[c][2] = in[c][2];
        out[c][3] = in[c][3];
    }
}

void BoxOf(const float (*corners)[4], float *out_max, float *out_min) {
    for (int lane = 0; lane < 4; lane++) {
        float hi = corners[0][lane];
        float lo = hi;

        for (int c = 1; c < 8; c++) {
            hi = vu0::Max(hi, corners[c][lane]);
            lo = vu0::Min(lo, corners[c][lane]);
        }

        out_max[lane] = hi;
        out_min[lane] = lo;
    }
}

} // namespace

void test1(float (*corners)[4], float (*screen)[4], float (*matrix)[4], float *out_max, float *out_min) {
    float product[4][4];

    MulMatrices(product, matrix, screen);
    TransformCorners(vu_corners, corners, product);
    BoxOf(vu_corners, out_max, out_min);
}

void test2(float *out_max, float *out_min) {
    float divided[8][4];

    DivideByW(divided, vu_corners);
    BoxOf(divided, out_max, out_min);
}

int mgInsideScreen(float (*corners)[4], float (*matrix)[4], float *out_max, float *out_min) {
    mgRENDER_INFO *render_info = &mgRenderInfo;
    float          product[4][4];
    float          transformed[8][4];
    float          divided[8][4];

    MulMatrices(product, matrix, render_info->world_screen_rel);
    TransformCorners(transformed, corners, product);
    DivideByW(divided, transformed);
    BoxOf(divided, out_max, out_min);

    return mgClipBoxW(out_max, out_min, render_info->screen_box_max, render_info->screen_box_min);
}

void mgCFrame::GetLocalMatrix(float (*matrix)[4]) {
    if (use_srt) {
        if (model_assets::UsesStandardScaleOrder(this)) {
            for (int column = 0; column < 3; ++column) {
                for (int row = 0; row < 4; ++row) {
                    matrix[column][row] = trans_matrix[column][row] * scale[column];
                }
            }
            sceVu0CopyVector(matrix[3], trans_matrix[3]);
        } else {
            ScaleColumns(matrix, trans_matrix, scale);
        }
        sceVu0FVECTOR translation;
        if (rot_type & MG_FRAME_ROT_LOCAL_ORIGIN) {
            sceVu0CopyVector(translation, matrix[3]);
            mgZeroVectorW(matrix[3]);
        }

        if (rot_type & MG_FRAME_ROT_APPLY) {
            if (rotation[0] != 0.0f) {
                sceVu0RotMatrixX(matrix, matrix, rotation[0]);
            }

            if (rotation[1] != 0.0f) {
                sceVu0RotMatrixY(matrix, matrix, rotation[1]);
            }

            if (rotation[2] != 0.0f) {
                sceVu0RotMatrixZ(matrix, matrix, rotation[2]);
            }
        }

        if (rot_type & MG_FRAME_ROT_LOCAL_ORIGIN) {
            sceVu0AddVector(matrix[3], translation, position);
            matrix[3][3] = 1.0f;
        } else {
            sceVu0AddVector(matrix[3], matrix[3], position);
            matrix[3][3] = 1.0f;
        }
    } else {
        sceVu0CopyMatrix(matrix, trans_matrix);
    }
}

PORT_OVERRIDE void mgCFrame::Initialize() {
    model_assets::ForgetFrame(this);
    elder = brother = child = parent = nullptr;
    sceVu0UnitMatrix(lw_matrix);
    sceVu0UnitMatrix(trans_matrix);
    name = nullptr;
    rot_type = 0;
    reference = 0;
    visual = nullptr;
    attr = nullptr;
    frame_num = 0;
    frame_list = nullptr;
    init_matrix = nullptr;
    bound = nullptr;
    mgCObject::Initialize();
}

void mgCFrame::GetBBoardMatrix(int mode, float (*matrix)[4], mgRENDER_INFO *info) {
    float position[4];
    float to_eye[4];
    // The w lane is never set; zero matches the scale vector's own convention and keeps the matrix's w column clear.
    float scale[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float world[4][4];
    float pitch[4][4];
    GetLWMatrix(world);
    scale[0] = mgDistVector(world[0]);
    scale[1] = mgDistVector(world[0]);
    scale[2] = mgDistVector(world[0]);
    std::memcpy(position, world[3], sizeof(position));
    sceVu0UnitMatrix(matrix);

    if (mode & 2) {
        sceVu0SubVector(matrix[2], info->camera_pos, position);
        matrix[2][1] = 0.0f;
        matrix[2][3] = 0.0f;
        sceVu0Normalize(matrix[2], matrix[2]);
        matrix[0][0] = matrix[2][2];
        matrix[0][2] = -matrix[2][0];
    }

    if (mode & 1) {
        sceVu0UnitMatrix(pitch);
        sceVu0SubVector(to_eye, info->camera_pos, position);
        sceVu0Normalize(to_eye, to_eye);
        float eye_y = to_eye[1];
        to_eye[3] = 0.0f;
        to_eye[1] = 0.0f;
        float horizontal = mgDistVector(to_eye);
        pitch[1][1] = horizontal;
        pitch[2][2] = horizontal;
        pitch[1][2] = -eye_y;
        pitch[2][1] = eye_y;
        sceVu0SubVector(matrix[2], info->camera_pos, position);
        matrix[2][1] = 0.0f;
        matrix[2][3] = 0.0f;
        sceVu0Normalize(matrix[2], matrix[2]);
        matrix[0][0] = matrix[2][2];
        matrix[0][2] = -matrix[2][0];
        mgMulMatrix(matrix, matrix, pitch);
    }

    ScaleColumns(matrix, matrix, scale);
    matrix[3][0] = position[0];
    matrix[3][1] = position[1];
    matrix[3][2] = position[2];
    sceVu0CopyMatrix(lw_matrix, matrix);
    changed = 0;
    ClearChildFlag();
}

void mgCFrame::GetLWMatrix(float (*matrix)[4]) {
    float parent_matrix[4][4];
    float local_matrix[4][4];

    if (reference != 0) {
        changed = 1;
    }

    if (changed == 0) {
        // The cached matrix holds unless an ancestor has changed.
        const mgCFrame *node = parent;
        while (node != nullptr && node->changed == 0) {
            node = node->parent;
        }
        if (node == nullptr) {
            sceVu0CopyMatrix(matrix, lw_matrix);
            return;
        }
    }

    ClearChildFlag();
    GetLocalMatrix(local_matrix);

    if (parent == nullptr) {
        sceVu0CopyMatrix(lw_matrix, local_matrix);
        sceVu0CopyMatrix(matrix, lw_matrix);
    } else {
        parent->GetLWMatrix(parent_matrix);
        MulMatrices(lw_matrix, local_matrix, parent_matrix);
        std::memcpy(matrix, lw_matrix, sizeof(float[4][4]));
    }
    changed = 0;
}

void mgCFrame::GetLWMatrixTopBottom(float (*matrix)[4]) {
    float parent_matrix[4][4];
    float local_matrix[4][4];

    if (reference != 0) {
        GetLWMatrix(matrix);
        return;
    }

    if (changed == 0) {
        mgCFrame *parent_frame = parent;

        if (parent_frame == nullptr) {
            sceVu0CopyMatrix(matrix, lw_matrix);
            return;
        }

        if (parent_frame->changed == 0) {
            sceVu0CopyMatrix(matrix, lw_matrix);
            return;
        }
    }

    ClearChildFlag();
    GetLocalMatrix(local_matrix);
    mgCFrame *parent = this->parent;

    if (parent == nullptr) {
        sceVu0CopyMatrix(lw_matrix, local_matrix);
        sceVu0CopyMatrix(matrix, lw_matrix);
    } else {
        sceVu0CopyMatrix(parent_matrix, parent->lw_matrix);
        MulMatrices(lw_matrix, local_matrix, parent_matrix);
        std::memcpy(matrix, lw_matrix, sizeof(float[4][4]));
    }

    changed = 0;
}

int mgCFrame::GetDrawRect(mgVu0FBOX *rect, mgCDrawManager *manager) {
    if (manager == nullptr) {
        manager = &mgDrawManager;
    }

    mgRENDER_INFO      *info = manager->render_info;
    const mgCFrameAttr *draw_attr = attr != nullptr ? attr : &dmy_attr;
    sceVu0FMATRIX       lw;

    if (attr != nullptr && attr->billboard != MG_FRAME_BILLBOARD_NONE) {
        GetBBoardMatrix(attr->billboard, lw, info);
    } else {
        GetLWMatrixTopBottom(lw);
    }

    sceVu0FVECTOR rect_max = {0.0f, 0.0f, 0.0f, 0.0f};
    sceVu0FVECTOR rect_min = {0.0f, 0.0f, 0.0f, 0.0f};
    int           visible = (draw_attr->draw & MG_FRAME_DRAW_VISIBLE) && visual != nullptr && bound != nullptr;

    if (visible) {
        sceVu0FMATRIX screen_matrix;
        float         transformed[8][4];
        float         divided[8][4];
        mgMulMatrix(screen_matrix, info->world_screen_rel, lw);
        TransformCorners(transformed, bound->corner, screen_matrix);
        DivideByW(divided, transformed);
        BoxOf(divided, rect_max, rect_min);

        // Cull against what the target shows, not the 4:3 screen: a wider window shows more at the sides.
        const mgPortExtent extent = mgPortVisibleExtent();
        const float        left = extent.left - 0.5f * mgScreenWidth;
        const float        top = extent.top - 0.5f * mgScreenHeight;
        const float        right = extent.right - 0.5f * mgScreenWidth;
        const float        bottom = extent.bottom - 0.5f * mgScreenHeight;
        visible = rect_min[0] <= right && rect_max[0] >= left && rect_min[1] <= bottom && rect_max[1] >= top &&
                  rect_max[3] >= info->clip_min[2];

        if (visible) {
            rect_min[0] += mgScreenWidth / 2;
            rect_max[0] += mgScreenWidth / 2;
            rect_min[1] += mgScreenHeight / 2;
            rect_max[1] += mgScreenHeight / 2;
        }
    }

    if (visible) {
        sceVu0CopyVector(rect->max, rect_max);
        sceVu0CopyVector(rect->min, rect_min);
    }

    if (draw_attr->draw & MG_FRAME_DRAW_SKIP_CHILDREN) {
        return visible;
    }

    for (mgCFrame *frame = child; frame != nullptr; frame = frame->brother) {
        mgVu0FBOX child_rect{};
        if ((frame->attr == nullptr || !(frame->attr->draw & MG_FRAME_DRAW_SKIP_BY_PARENT)) &&
            frame->GetDrawRect(&child_rect, nullptr)) {
            if (!visible) {
                sceVu0CopyVector(rect_max, child_rect.max);
                sceVu0CopyVector(rect_min, child_rect.min);
            } else {
                mgVectorMaxMin(rect_max, rect_min, rect_max, rect_min, child_rect.max, child_rect.min);
            }

            visible = 1;
        }
    }

    sceVu0CopyVector(rect->max, rect_max);
    sceVu0CopyVector(rect->min, rect_min);
    return visible;
}
