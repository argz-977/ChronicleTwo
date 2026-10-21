#include "common.h"

#include <cstring>
#include <utility>
#include <vector>

#include "gameutil.hpp"
#include "mg_math.hpp"
#include "stubs/gameutil.hpp"
#include "vu0_lanes.hpp"

namespace {
// Retail clears OldSkinFrame after each skin pass; DeformMesh is its only caller, so clearing at the motion's
// first skin list starts every pass from the same state.
mgCFrame *cached_skin_frame;

// One vertex's accumulated deformation, aligned like the quadword it replaces.
struct alignas(16) DeformVertex {
    float xyzw[4];
};

// Accumulated deformation of each vertex of the skin being deformed. Retail's 800-entry def_vrtx took
// its vertex numbers from the mesh's key lists unchecked; this table is sized to the skin's vertices.
std::vector<DeformVertex> deform_vertices;

// Whether the segment from..to, bounded by seg_max/seg_min, crosses poly; the crossing is stored in point.
bool SegmentCrossesPoly(CCPoly &poly, float *from, float *to, const float *seg_max, const float *seg_min, float *point) {
    float poly_min[4];
    float poly_max[4];
    float offset[4];

    mgVectorMaxMin(poly_max, poly_min, poly.vertex[0], poly.vertex[1], poly.vertex[2]);

    if (seg_max[0] < poly_min[0] || seg_max[1] < poly_min[1] || seg_max[2] < poly_min[2]) {
        return false;
    }

    if (!(seg_min[0] <= poly_max[0]) || !(seg_min[1] <= poly_max[1]) || !(seg_min[2] <= poly_max[2])) {
        return false;
    }

    sceVu0SubVector(offset, from, poly.vertex[0]);
    const float d0 = sceVu0InnerProduct(poly.normal, offset);
    sceVu0SubVector(offset, to, poly.vertex[0]);
    const float d1 = sceVu0InnerProduct(poly.normal, offset);

    if ((d0 > 0.0f && d1 > 0.0f) || (d0 < 0.0f && d1 < 0.0f)) {
        return false;
    }

    return mgIntersectionPoint_line_poly3(from, to, poly.vertex[0], poly.vertex[1], poly.vertex[2], poly.normal, point) != 0;
}
} // namespace

void testVUnew(float (*matrix)[4], float *vertex, float *weight, float *accum, float *out) {
    float transformed[4];
    vu0::Transform(transformed, matrix, vertex);

    float result[4];

    for (int lane = 0; lane < 3; lane++) {
        result[lane] = accum[lane] + transformed[lane] * weight[0];
    }

    // The xyz-masked vmulx leaves w holding matrix row 2 from the lqc2 above, and the full-width vadd adds that
    // into the accumulator's w.
    result[3] = accum[3] + matrix[2][3];

    std::memcpy(accum, result, sizeof(result));
    std::memcpy(out, result, sizeof(result));
}

Mot_List *MotionProc2(mgCFrame *root, tagMOTION_TYPE *motion, tagFRAME_INF *frame_info, Mot_List *list) {
    static sceVu0FVECTOR *vert;
    static sceVu0FMATRIX  tmp_SkinMatrix;
    static sceVu0FMATRIX  tmp_SkinMatrix_inv;
    static sceVu0FMATRIX  tmp_ChrMatrix;
    static sceVu0FMATRIX  tmp_BaseSkinMatrix;
    static sceVu0FMATRIX  tmp_BaseSkinMatrix_inv;
    float                 deform[4][4];
    float                 bone_matrix[4][4];
    float                 bone_in_skin[4][4];
    float                 skin_bone[4][4];
    float                 bone_in_skin_inv[4][4];
    float                 bone_base[4][4];
    float                 moved[4];
    float                 weight[4];
    mgCFrame             *bone = nullptr;
    mgCFrame             *skin = nullptr;
    unsigned int          i = 0;
    int                   vertex = 0;

    if (list == motion->skin_list) {
        cached_skin_frame = nullptr;
    }

    if (list->key_count == 0) {
        return list->next;
    }

    bone = root->GetFrame(list->target);
    skin = root->GetFrame(list->frame);

    if (cached_skin_frame != skin) {
        cached_skin_frame = skin;
        vert = static_cast<mgCVisualMDT *>(skin->visual)->vertex;

        deform_vertices.assign(frame_info[list->frame].vertex_count, DeformVertex{
                                                                         {0.0f, 0.0f, 0.0f, 1.0f}
        });

        skin->GetLWMatrix(tmp_SkinMatrix);
        root->GetLWMatrix(tmp_ChrMatrix);
        mgInversMatrix(tmp_SkinMatrix_inv, tmp_SkinMatrix);
        mgMulMatrix(tmp_BaseSkinMatrix, tmp_ChrMatrix, motion->base_matrices[list->frame]);
        mgInversMatrix(tmp_BaseSkinMatrix_inv, tmp_BaseSkinMatrix);
    }

    sceVu0UnitMatrix(bone_matrix);
    sceVu0UnitMatrix(tmp_SkinMatrix);
    bone->GetLWMatrix(bone_matrix);
    mgMulMatrix(bone_base, tmp_ChrMatrix, motion->base_matrices[list->target]);
    mgMulMatrix(bone_in_skin, tmp_BaseSkinMatrix_inv, bone_base);
    mgInversMatrix(bone_in_skin_inv, bone_in_skin);
    mgMulMatrix(skin_bone, tmp_SkinMatrix_inv, bone_matrix);
    mgMulMatrix(deform, skin_bone, bone_in_skin_inv);

    for (i = 0; i < list->key_count; i++) {
        weight[0] = 0.01f * list->values[i][0];
        if (list->key_frames[i] >= deform_vertices.size()) {
            continue;
        }
        vertex = static_cast<int>(list->key_frames[i]);
        float *accum = deform_vertices[vertex].xyzw;

        if (list->type == MOTION_KEY_SKIN_WEIGHTED) {
            testVUnew(deform, frame_info[list->frame].base_vertices[vertex], weight, accum, vert[vertex]);
        } else {
            sceVu0ApplyMatrix(moved, deform, frame_info[list->frame].base_vertices[vertex]);
            moved[3] = 0.0f;
            accum[0] += moved[0];
            accum[1] += moved[1];
            accum[2] += moved[2];
            accum[0] = vu0::Div(accum[0], accum[3]);
            accum[1] = vu0::Div(accum[1], accum[3]);
            accum[2] = vu0::Div(accum[2], accum[3]);
            sceVu0CopyVectorXYZ(vert[vertex], accum);
            accum[3] += 1.0f;
        }
    }

    return list->next;
}

int CheckHit(CollisionInfo *collision, float *from, float *to, float *hit, int closest, int mask) {
    if (collision == nullptr) {
        return 0;
    }

    float seg_max[4];
    float seg_min[4];
    mgVectorMaxMin(seg_max, seg_min, from, to);

    CCPoly   *poly = collision->polys;
    const int count = collision->count;
    if (poly == nullptr || count == 0) {
        return -1;
    }

    int   best = -1;
    bool  has_best = false;
    float best_dist = 0.0f;
    for (int i = 0; i < count; i++, poly++) {
        float point[4];
        if ((poly->ignore_mask & mask) || !SegmentCrossesPoly(*poly, from, to, seg_max, seg_min, point)) {
            continue;
        }

        if (closest == 0) {
            best = i;
            sceVu0CopyVector(hit, point);
            break;
        }

        const float diff[3] = {from[0] - point[0], from[1] - point[1], from[2] - point[2]};
        const float dist = (diff[0] * diff[0]) + (diff[1] * diff[1]) + (diff[2] * diff[2]);
        if (!has_best || !(best_dist <= dist)) {
            best_dist = dist;
            best = i;
            sceVu0CopyVector(hit, point);
        }
        has_best = true;
    }

    return best;
}

int CheckHits(CollisionInfo *info, float *from, float *to, int max_hits, int *hit_polys, float (*hit_points)[4], int sort, int ignore_mask) {
    float seg_max[4];
    float seg_min[4];
    mgVectorMaxMin(seg_max, seg_min, from, to);

    int     hits = 0;
    CCPoly *poly = info->polys;
    for (int i = 0; i < info->count; i++, poly++) {
        float point[4];
        if ((poly->ignore_mask & ignore_mask) || !SegmentCrossesPoly(*poly, from, to, seg_max, seg_min, point)) {
            continue;
        }

        if (hits >= max_hits) {
            break;
        }

        hit_polys[hits] = i;
        sceVu0CopyVector(hit_points[hits], point);
        hit_points[hits][3] = mgDistVector(from, point);
        hits++;
    }

    if (sort == 0) {
        return hits;
    }

    // Retail sorts ascending whatever the sign of sort.
    for (int i = 0; i < hits - 1; i++) {
        for (int j = i + 1; j < hits; j++) {
            if (!(hit_points[i][3] <= hit_points[j][3])) {
                std::swap(hit_polys[i], hit_polys[j]);
                float swap[4];
                sceVu0CopyVector(swap, hit_points[i]);
                sceVu0CopyVector(hit_points[i], hit_points[j]);
                sceVu0CopyVector(hit_points[j], swap);
            }
        }
    }

    return hits;
}
