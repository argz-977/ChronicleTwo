// Ahead of common.h: libgraph.h defines A, B, C and D as macros that would rewrite the vu1 header's names.
// clang-format off
#include "vu1/water_geom.hpp"

#include "common.h"
// clang-format on

#include "water.hpp"

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

#include "mg_drawenv.hpp"
#include "mg_drawprim.hpp"
#include "mg_math.hpp"
#include "mg_visual.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"

namespace vu = dc::vu1;

namespace {

constexpr uint32_t kWaterPrimMode = 0x158u;
constexpr uint32_t kWaterPrimFog = 0x20u;

// TEXA as the water packet writes it: TA0 and TA1 0x80, AEM off.
constexpr uint64_t kWaterTexa = 0x0000008000000080ull;

void MultiplyColumnMajor(const float (&a)[16], const float (&b)[16], float (&out)[16]) {
    float result[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            result[col * 4 + row] = sum;
        }
    }
    std::memcpy(out, result, sizeof result);
}

bool FogOn(const mgRENDER_INFO &info) {
    return info.attr != nullptr && info.attr->fog != 0 && info.fog_enable != 0;
}

void FogColour(const mgRENDER_INFO &info, uint8_t (&out)[3]) {
    out[0] = info.fog.r;
    out[1] = info.fog.g;
    out[2] = info.fog.b;

    if (info.attr != nullptr && info.attr->fog == MG_FOG_BLACK) {
        out[0] = out[1] = out[2] = 0x00;
    } else if (info.attr != nullptr && info.attr->fog == MG_FOG_WHITE) {
        out[0] = out[1] = out[2] = 0xFF;
    }
}

// The overrides mgCVisual::SetDrawEnvGifTag applies to the base draw env, in native terms.
void ApplyFrameAttr(gfx::DrawState &state, const mgCVisualAttr &attr) {
    if (attr.alpha_ref >= 0) {
        state.alpha_ref = static_cast<uint8_t>(attr.alpha_ref);
    }

    switch (attr.z_test) {
        case MG_DEPTH_TEST_ALWAYS:
            state.depth_test = gfx::DepthTest::Always;
            break;
        case MG_DEPTH_TEST_GEQUAL:
            state.depth_test = gfx::DepthTest::GEqual;
            break;
        case MG_DEPTH_TEST_GREATER:
            state.depth_test = gfx::DepthTest::Greater;
            break;
        default:
            break;
    }

    if (attr.alpha_test > 0) {
        state.alpha_test = true;
        state.alpha_func = static_cast<gfx::AlphaFunc>(attr.alpha_test);
    } else if (attr.alpha_test == -1) {
        state.alpha_test = false;
    }

    // dest_alpha_test has no DrawState equivalent.

    switch (attr.alpha_blend) {
        case MG_ALPHA_BLEND_NORMAL:
            state.alpha = gfx::GsBlend{.a = 0, .b = 1, .c = 0, .d = 1, .fix = 0x00};
            break;
        case MG_ALPHA_BLEND_ADD:
            state.alpha = gfx::GsBlend{.a = 0, .b = 2, .c = 0, .d = 1, .fix = 0x00};
            break;
        case MG_ALPHA_BLEND_SUB:
            state.alpha = gfx::GsBlend{.a = 2, .b = 0, .c = 0, .d = 1, .fix = 0x00};
            break;
        case MG_ALPHA_BLEND_NONE:
            state.alpha = gfx::GsBlend{.a = 2, .b = 2, .c = 2, .d = 0, .fix = 0x80};
            break;
        case MG_ALPHA_BLEND_ADD_FULL:
            state.alpha = gfx::GsBlend{.a = 0, .b = 2, .c = 2, .d = 1, .fix = 0x80};
            break;
        default:
            break;
    }

    switch (attr.z_write) {
        case MG_ZBUF_NO_WRITE:
            state.depth_write = false;
            break;
        case MG_ZBUF_WRITE:
            state.depth_write = true;
            break;
        default:
            break;
    }
}

} // namespace

PORT_OVERRIDE uptr CWater::CreatePacket(mgCDrawManager * /*draw_manager*/) {
    packet = 0;
    return 0;
}

PORT_OVERRIDE int CWater::CreateRenderInfoPacket(u_int * /*packet*/, float (* /*matrix*/)[4], mgRENDER_INFO *info) {
    info->scissor = 0;
    prmode = kWaterPrimMode | (FogOn(*info) ? kWaterPrimFog : 0u);
    return 0;
}

PORT_OVERRIDE int CWater::Draw(u_int * /*tag*/, float (*matrix)[4], mgCDrawManager *draw_manager) {
    if (draw_manager == nullptr) {
        draw_manager = &mgDrawManager;
    }

    mgRENDER_INFO *render_info = draw_manager->render_info;
    texture_manager = draw_manager->texture_manager;
    CreateRenderInfoPacket(nullptr, matrix, render_info);

    uint8_t fog_colour[3];
    FogColour(*render_info, fog_colour);

    // The water packet leaves TEXA, FOGCOL and TEX1 in the GS, where later draws find them.
    mgPortGs().texa = kWaterTexa;
    for (int channel = 0; channel < 3; ++channel) {
        mgPortGs().fog_color[channel] = fog_colour[channel];
    }
    if (texture != nullptr) {
        mgPortGs().tex1 = texture->tex1_bits;
    }

    if (!mgPortRendererReady() || !mgPortFrameOpen() || rows < 2 || columns < 2 || height == nullptr) {
        return 0;
    }

    sceVu0FMATRIX model_to_eye;
    mgMulMatrix(model_to_eye, render_info->view, matrix);
    vu::Mat4 eye{};
    std::memcpy(&eye, model_to_eye, sizeof eye);

    const vu::WaterSurface surface{
        .p0 = surface_param0,
        .p1 = surface_param1,
        .colour = {static_cast<float>(color[0]), static_cast<float>(color[1]), static_cast<float>(color[2]), static_cast<float>(color[3])}
    };
    const vu::WaterMeshDesc desc{
        .min = {.x = min[0], .y = min[1], .z = min[2], .w = min[3]},
        .max = {.x = max[0], .y = max[1], .z = max[2], .w = max[3]},
        .rows = rows,
        .columns = columns
    };
    const vu::WaterMesh mesh = vu::BuildWaterMesh(desc, std::span<const float>(height, static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns)));

    std::vector<vu::WaterHostVertex> host_vertices;
    std::vector<uint32_t>            indices;
    vu::WaterHostMesh(surface, eye, mesh, host_vertices, indices);
    if (indices.empty()) {
        return 0;
    }

    // Retail samples a copy of the frame that CMap::DrawWater takes right before this draw; the
    // same frame contents come from a snapshot taken here.
    gfx::TextureHandle image = gfx::kNullTexture;
    if (mgPortCurrentTarget() == gfx::kMainTarget) {
        image = gfx::NamedRenderTarget("water refraction", static_cast<uint32_t>(mgScreenWidth),
                                       static_cast<uint32_t>(mgScreenHeight), false, false, true);
        if (image == gfx::kNullTexture || !gfx::SnapshotFrame(image)) {
            return 0;
        }
    } else if (texture != nullptr) {
        image = mgPortBinding(texture).texture;
    }
    if (image == gfx::kNullTexture) {
        return 0;
    }

    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(image);
    if (mapping.pixel_width == 0 || mapping.pixel_height == 0) {
        return 0;
    }

    std::vector<gfx::Vertex3D> vertices(host_vertices.size());
    for (std::size_t i = 0; i < host_vertices.size(); ++i) {
        const vu::WaterHostVertex &host = host_vertices[i];
        gfx::Vertex3D             &vertex = vertices[i];
        std::copy(host.position, host.position + 3, vertex.position);
        vertex.uv[0] = host.uv_offset[0] * mapping.scale_x / static_cast<float>(mapping.pixel_width);
        vertex.uv[1] = host.uv_offset[1] * mapping.scale_y / static_cast<float>(mapping.pixel_height);
        std::copy(host.color, host.color + 4, vertex.color);
    }

    float model[16];
    std::memcpy(model, matrix, sizeof model);
    float world_to_clip[16];
    mgPortWorldToClip(world_to_clip);

    gfx::MeshConstants constants{};
    MultiplyColumnMajor(world_to_clip, model, constants.mvp);
    constants.flags = gfx::kMeshVertexColor | gfx::kMeshScreenUv;
    for (float &channel : constants.diffuse) {
        channel = 1.0f;
    }

    const bool fog_on = FogOn(*render_info);
    if (fog_on) {
        // mesh.vert clamps to [lo, hi]; the VU takes max(far, min(near, F)). Those agree when
        // far <= near, and otherwise the VU's value is the constant far.
        constants.flags |= gfx::kMeshFog;
        constants.fog[0] = render_info->fog.offset;
        constants.fog[1] = render_info->fog.scale;
        constants.fog[2] = render_info->fog.far_value;
        constants.fog[3] = std::max(render_info->fog.near_value, render_info->fog.far_value);
    }

    const mgCDrawEnv &base = draw_env != nullptr ? *draw_env : render_info->draw_env[0];
    gfx::DrawState    state = mgPortDrawState();
    mgPortApplyDrawEnv(state, base);
    if (render_info->attr != nullptr) {
        ApplyFrameAttr(state, *render_info->attr);
    }
    state.blend = true;
    state.fog = fog_on;
    for (int channel = 0; channel < 3; ++channel) {
        state.fog_color[channel] = fog_colour[channel];
    }
    state.texa_aem = false;
    state.texa_ta0 = 0x80;
    state.cull = gfx::CullMode::None;

    gfx::TextureBinding binding;
    binding.texture = image;
    binding.filter = texture != nullptr ? mgPortFilter(texture->tex1_bits) : gfx::Filter::Linear;
    binding.wrap_u = gfx::Wrap::Clamp;
    binding.wrap_v = gfx::Wrap::Clamp;
    binding.ignore_alpha = true;

    const gfx::MeshTransform transform = mgPortMeshTransform(model);
    mgPortInterpScope        scope(this, kMgPortFrameTeleportDistance);
    mgPortDrawMeshImmediate(vertices, indices, constants, binding, state, &transform);
    return 0;
}
