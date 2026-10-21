#include "common.h"

#include <cstring>
#include <vector>

#include "editmap.hpp"
#include "stubs/editmap2.hpp"

int CheckFenceChain(CEditParts *a, CEditParts *b);

void PlaneNormalXZ(float *normal, float *p0, float *p1, float *p2) {
    const float edge0[4] = {p1[0] - p0[0], 0.0f, p1[2] - p0[2], 0.0f};
    const float edge1[4] = {p2[0] - p0[0], 0.0f, p2[2] - p0[2], 0.0f};

    normal[0] = edge0[1] * edge1[2] - edge1[1] * edge0[2];
    normal[1] = edge0[2] * edge1[0] - edge1[2] * edge0[0];
    normal[2] = edge0[0] * edge1[1] - edge1[0] * edge0[1];
    // vopmsub.xyz never writes w, so the retail store takes a stale lane; zero is the deterministic stand-in.
    normal[3] = 0.0f;
}

// Retail's paint of a fence and the fences joined to it, gathering every other placed fence into a list that grows
// with the town.
PORT_OVERRIDE int CEditMap::PaintFence(int index, float *color, int count) {
    CEditParts *fence = GetePlaceParts(index);
    if (fence == nullptr || fence->info == nullptr || fence->IsFence() == 0 || fence->info->paint_num <= 0) {
        return 0;
    }
    std::vector<CEditParts *> candidates;
    for (int i = 0; i < edit_parts_max; ++i) {
        CEditParts *part = &edit_parts[i];
        if (CheckNormalPlaceParts(part) != 0 && part->IsFence() != 0 && part->info->paint_num > 0 && part != fence) {
            candidates.push_back(part);
        }
    }
    fence_num = static_cast<s32>(candidates.size());
    candidates.push_back(nullptr);
    fence_list = candidates.data();
    std::memcpy(fence_color, color, sizeof(fence_color));
    paint_num = count;
    const int painted = PaintFence(fence);
    fence_list = nullptr;
    return painted;
}

// Paints the fences of fence_list joined to a fence, directly or through others, as retail's recursion does.
PORT_OVERRIDE int CEditMap::PaintFence(CEditParts *fence) {
    int  painted = 0;
    auto paint = [&](CEditParts *part) {
        part->SetColor(0, fence_color);
        part->UpdateColor();
        ++painted;
        --paint_num;
    };
    paint(fence);
    std::vector<CEditParts *> pending{fence};
    while (!pending.empty()) {
        CEditParts *joined_to = pending.back();
        pending.pop_back();
        for (int i = 0; i < fence_num; ++i) {
            fence_now = fence_list[i];
            if (fence_now != nullptr && CheckFenceChain(joined_to, fence_now) != 0) {
                fence_list[i] = nullptr;
                paint(fence_now);
                pending.push_back(fence_now);
            }
        }
    }
    return painted;
}
