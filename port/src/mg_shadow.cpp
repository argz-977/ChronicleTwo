#include "common.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "mg_drawenv.hpp"
#include "mg_math.hpp"
#include "mg_shadow.hpp"
#include "mg_shadow_port.hpp"
#include "mg_visual.hpp"
#include "mglib_port.hpp"
#include "vu1/sdw_geom.hpp"

namespace vu = dc::vu1;

namespace {

constexpr float kIdentity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

// Flat colour 1 is retail's RGBAQ (1, 1, 1) against a material of 0x80.
constexpr float kShadowDiffuse = 1.0f / 128.0f;
constexpr float kShadowAlpha = 0.5f;

// Z-fail counts the volume's far caps, which lie on the receiving plane. A cap on the plane passes
// GEQUAL against the floor and is missed, so the drop is pushed past the plane by this fraction of
// its eye depth.
constexpr float kShadowDropBias = 1e-3f;

// GS guard band half-range about the screen centre, in logical pixels.
constexpr float kGuardHalfRange = 2048.0f;

vu::Mat4 ToMat(const float (*m)[4]) {
    vu::Mat4 out{};
    std::memcpy(&out, m, sizeof out);
    return out;
}

gfx::StencilFace VolumeFace(gfx::StencilOp depth_fail) {
    return {.compare = gfx::CompareOp::Always, .fail = gfx::StencilOp::Keep, .pass = gfx::StencilOp::Keep, .depth_fail = depth_fail};
}

// Negated comparisons so a NaN corner is rejected.
bool InClipVolume(const vu::Vec4 &p, const mgPortProjection &projection) {
    if (p.z < projection.near_z || p.z > projection.far_z) {
        return false;
    }

    const float screen_x = std::fabs(p.x * projection.eye / p.z);
    const float screen_y = std::fabs(p.y * projection.eye * projection.aspect_y / p.z);
    return screen_x <= kGuardHalfRange && screen_y <= kGuardHalfRange;
}

} // namespace

void AppendShadowPrism(const vu::Vec4 top[3], const vu::Vec4 drop[3], const vu::Vec4 &bias_dir,
                       const mgPortProjection &projection, std::vector<vu::SdwPrismTriangle> &out) {
    for (int j = 0; j < 3; ++j) {
        if (!InClipVolume(top[j], projection) || !InClipVolume(drop[j], projection)) {
            return;
        }
    }

    const float bias = kShadowDropBias * std::fabs(drop[0].z);
    vu::Vec4    biased[3];
    for (int j = 0; j < 3; ++j) {
        biased[j] = drop[j] + bias_dir * bias;
    }

    // Grazing triangles are rejected by SdwPrism's |thickness| <= 1e-4 |n||h| test, retail's |Q| <= 1e-4 up to scale.
    vu::SdwPrism(top, biased, out);
}

PORT_OVERRIDE int mgCShadowMDT::CreateFacePacket(u_int * /*packet*/, mgCFace * /*face*/) {
    return 0;
}

PORT_OVERRIDE uptr mgCShadowMDT::CreatePacket(mgCDrawManager * /*draw_manager*/) {
    for (mgFACE_GROUP *node = face_group; node != nullptr; node = node->next) {
        node->packet = nullptr;
        node->packet_size = 0;
    }

    return 0;
}

PORT_OVERRIDE int mgCShadowMDT::CreateRenderInfoPacket(u_int * /*packet*/, float (*matrix)[4], mgRENDER_INFO *info) {
    if (!mgPortRendererReady() || !mgPortFrameOpen() || !mgPortInShadow()) {
        return 0;
    }

    sceVu0FMATRIX model_to_eye;
    sceVu0FMATRIX drop_to_eye;
    sceVu0FMATRIX shadow;
    mgMulMatrix(model_to_eye, info->view, matrix);
    mgMulMatrix(shadow, info->shadow, matrix);
    mgMulMatrix(drop_to_eye, info->view, shadow);

    const vu::Mat4                    to_eye = ToMat(model_to_eye);
    const vu::Mat4                    drop_eye = ToMat(drop_to_eye);
    const vu::Vec4                    light_world = {.x = info->shadow_light_dir[0], .y = info->shadow_light_dir[1], .z = info->shadow_light_dir[2], .w = 0.0f};
    const vu::Vec4                    light_eye = vu::Mul(ToMat(info->view), light_world);
    const vu::Vec4                    bias_dir = light_eye * (1.0f / std::sqrt(vu::Dot3(light_eye, light_eye)));
    const mgPortProjection            projection = mgPortCurrentProjection();
    std::vector<vu::SdwPrismTriangle> prisms;

    for (mgFACE_GROUP *group = face_group; group != nullptr; group = group->next) {
        for (mgCFace *face = group->face; face != nullptr; face = face->next) {
            for (int t = 0; t < face->vertex_num; ++t) {
                vu::Vec4 corner[3];
                for (int j = 0; j < 3; ++j) {
                    const float *p = vertex[face->index[3 * t + j]];
                    corner[j] = {.x = p[0], .y = p[1], .z = p[2], .w = 1.0f};
                }

                vu::Vec4 top[3];
                vu::Vec4 drop[3];
                for (int j = 0; j < 3; ++j) {
                    top[j] = vu::Mul(to_eye, corner[j]);
                    drop[j] = vu::Mul(drop_eye, corner[j]);
                }
                AppendShadowPrism(top, drop, bias_dir, projection, prisms);
            }
        }
    }

    if (prisms.empty()) {
        return 0;
    }

    std::vector<gfx::Vertex3D> vertices;
    std::vector<uint32_t>      indices;
    vertices.reserve(prisms.size() * 3);
    indices.reserve(prisms.size() * 3);
    for (const vu::SdwPrismTriangle &tri : prisms) {
        for (const vu::Vec4 &v : tri.v) {
            gfx::Vertex3D vertex{};
            vertex.position[0] = v.x;
            vertex.position[1] = v.y;
            vertex.position[2] = v.z;
            indices.push_back(static_cast<uint32_t>(vertices.size()));
            vertices.push_back(vertex);
        }
    }

    // Vertices are in eye space; the local transform carries them back to world so the display list's
    // interpolated camera can be applied on top.
    gfx::MeshTransform transform = mgPortMeshTransform(kIdentity);
    if (!gfx::InvertAffineTransform(transform.view, transform.local)) {
        return 0;
    }

    gfx::MeshConstants constants{};
    mgPortEyeToClip(projection, mgPortCurrentTarget(), constants.mvp);
    constants.flags = gfx::kMeshShadow;
    constants.diffuse[0] = kShadowDiffuse;
    constants.diffuse[1] = kShadowDiffuse;
    constants.diffuse[2] = kShadowDiffuse;
    constants.diffuse[3] = kShadowAlpha;

    const mgCDrawEnv &env = draw_env != nullptr ? *draw_env : info->draw_env[0];

    // Z-fail: a pixel's count is the number of volumes its surface lies inside. Colour is not written.
    gfx::DrawState volume = mgPortDrawState();
    mgPortApplyDrawEnv(volume, env);
    volume.blend = false;
    volume.alpha_test = false;
    volume.cull = gfx::CullMode::None;
    volume.depth_test = gfx::DepthTest::GEqual;
    volume.depth_write = false;
    volume.stencil_test = true;
    volume.stencil_front = VolumeFace(gfx::StencilOp::DecrementWrap);
    volume.stencil_back = VolumeFace(gfx::StencilOp::IncrementWrap);
    volume.color_write_mask = 0;

    // Shadow volumes are rebuilt from this tick's matrices, so the previous tick's must not be
    // interpolated into them.
    {
        mgPortInterpScope scope(this, kMgPortFrameTeleportDistance, false, true);
        mgPortDrawMeshImmediate(vertices, indices, constants, gfx::TextureBinding{}, volume, &transform);
    }

    return 0;
}
