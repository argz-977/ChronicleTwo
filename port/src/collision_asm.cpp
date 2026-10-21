#include "common.h"

#include <cstring>

#include "collision.hpp"
#include "mg_math.hpp"
#include "stubs/collision.hpp"
#include "vu0_lanes.hpp"

namespace {

// Retail parks the matrix in vf10-vf13 between pre_trance_normal and the trance_normal calls after it.
float trance_matrix[4][4];

} // namespace

int CCollisionMDT::PickUpNearPoly(CCPoly *out, const mgVu0FBOX &box, int max) {
    sceVu0FVECTOR box_max;
    sceVu0FVECTOR box_min;
    sceVu0FVECTOR poly_max;
    sceVu0FVECTOR poly_min;
    int           i = 0;
    int           count = 0;
    CCPoly       *polygon = nullptr;

    if (poly == nullptr) {
        return 0;
    }

    if (!(box.min[0] <= bbox.max[0])) {
        return 0;
    }

    if (!(box.min[1] <= bbox.max[1])) {
        return 0;
    }

    if (!(box.min[2] <= bbox.max[2])) {
        return 0;
    }

    if (box.max[0] < bbox.min[0]) {
        return 0;
    }

    if (box.max[1] < bbox.min[1]) {
        return 0;
    }

    if (box.max[2] < bbox.min[2]) {
        return 0;
    }

    i = 0;
    count = 0;
    const volatile mgVu0FBOX &volatile_box = box;
    box_max[0] = volatile_box.max[0];
    box_max[1] = volatile_box.max[1];
    box_max[2] = volatile_box.max[2];
    box_max[3] = 1.0f;
    box_min[0] = box.min[0];
    box_min[1] = box.min[1];
    box_min[2] = box.min[2];
    box_min[3] = 1.0f;
    polygon = poly;
    for (; i < poly_count; i++, polygon++) {
        mgVectorMaxMin(poly_max, poly_min, polygon->vertex[0], polygon->vertex[1], polygon->vertex[2]);

        if (mgClipBox(box_max, box_min, poly_max, poly_min)) {
            std::memcpy(out->vertex[0], polygon->vertex[0], sizeof(sceVu0FVECTOR));
            std::memcpy(out->vertex[1], polygon->vertex[1], sizeof(sceVu0FVECTOR));
            std::memcpy(out->vertex[2], polygon->vertex[2], sizeof(sceVu0FVECTOR));
            out->ground_kind = polygon->ground_kind;
            out->foot_sound = polygon->foot_sound;
            out->area_kind = polygon->area_kind;
            out->ignore_mask = polygon->ignore_mask;
            out->parts_no = polygon->parts_no;
            out->attr = polygon->attr;
            out->attr_value = polygon->attr_value;
            std::memcpy(out->normal, polygon->normal, sizeof(sceVu0FVECTOR));
            max--;
            count++;
            out++;

            if (max <= 0) {
                break;
            }
        }
    }

    return count;
}

void pre_trance_normal(float (*matrix)[4]) {
    std::memcpy(trance_matrix, matrix, sizeof(trance_matrix));
}

// Retail loads the three corners from v0 onward and stores them back through v0, v1 and v2; every
// caller passes contiguous corners, so the reads and the writes agree.
void trance_normal(float *v0, float *v1, float *v2, float *normal) {
    float corners[3][4];
    float transformed[3][4];

    std::memcpy(corners, v0, sizeof(corners));

    for (int corner = 0; corner < 3; corner++) {
        vu0::Transform(transformed[corner], trance_matrix, corners[corner]);
    }

    std::memcpy(v0, transformed[0], sizeof(transformed[0]));
    std::memcpy(v1, transformed[1], sizeof(transformed[1]));
    std::memcpy(v2, transformed[2], sizeof(transformed[2]));

    float edge0[3];
    float edge1[3];

    for (int lane = 0; lane < 3; lane++) {
        edge0[lane] = transformed[1][lane] - transformed[0][lane];
        edge1[lane] = transformed[2][lane] - transformed[0][lane];
    }

    normal[0] = edge0[1] * edge1[2] - edge1[1] * edge0[2];
    normal[1] = edge0[2] * edge1[0] - edge1[2] * edge0[0];
    normal[2] = edge0[0] * edge1[1] - edge1[0] * edge0[1];
    // vopmsub.xyz never writes w, so the retail store takes a stale lane; zero is the deterministic stand-in.
    normal[3] = 0.0f;
}
