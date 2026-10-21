#include "common.h"

#include <libvu0.h>

#include <vector>

#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "vu0_lanes.hpp"
#include "wavetable.hpp"

namespace {

float Intensity(float slope) {
    const float intensity = 40.0f + 540.0f * slope;

    if (!(intensity <= 200.0f)) {
        return 200.0f;
    }

    if (intensity < 0.0f) {
        return 0.0f;
    }

    return intensity;
}

void Shade(uint8_t rgba[4], float slope) {
    const auto value = static_cast<uint8_t>(vu0::Ftoi(Intensity(slope)));
    rgba[0] = value;
    rgba[1] = value;
    rgba[2] = value;
    rgba[3] = 96;
}

} // namespace

PORT_OVERRIDE void CWaveTable::CreateTexture(mgCTexture *output_texture) {
    if (output_texture == nullptr) {
        return;
    }

    if (output_texture->bpp < 24) {
        return;
    }

    mgSetPkFrameBuffer(output_texture);
    mgCDrawPrim prim;
    prim.Initialize(nullptr, nullptr);
    prim.DepthTestEnable(0);
    prim.ZMask(MG_Z_MASK_MASKED);
    prim.Shading(1);
    prim.AlphaBlendEnable(1);

    const float cell_width = static_cast<float>(output_texture->width) / 23.0f;
    const float cell_height = static_cast<float>(output_texture->height) / 23.0f;
    const auto  origin_x = static_cast<float>(mgScreenOffx);
    const auto  origin_y = static_cast<float>(mgScreenOffy);
    prim.Begin2();

    float row_offset = 0.0f;

    for (int row = 0; row < WAVE_TABLE_DIM - 1; row++, row_offset += 1.0f) {
        const float               *line = height[current][row];
        const float               *next_line = height[current][(row + 1) % WAVE_TABLE_DIM];
        std::vector<gfx::Vertex2D> strip;
        float                      column_offset = 0.0f;

        for (int column = 0; column < WAVE_TABLE_DIM; column++, column_offset += 1.0f) {
            const int sample_column = column >= WAVE_TABLE_DIM - 1 ? 0 : column;
            const int next_column = (sample_column + 1) % WAVE_TABLE_DIM;

            const float x = origin_x + column_offset * cell_width;
            const float y = origin_y + row_offset * cell_height;
            const float bottom_y = y + cell_height;

            uint8_t top_rgba[4];
            uint8_t bottom_rgba[4];
            Shade(top_rgba, line[sample_column] - line[next_column]);
            Shade(bottom_rgba, next_line[sample_column] - next_line[next_column]);

            strip.push_back(mgPortPrimVertex(prim, x - origin_x, y - origin_y, 0.0f, 0.0f, 0.0f, top_rgba, 0));
            strip.push_back(
                mgPortPrimVertex(prim, x - origin_x, bottom_y - origin_y, 0.0f, 0.0f, 0.0f, bottom_rgba, 0));
        }

        std::vector<gfx::Vertex2D> triangles;
        mgPortStripTriangles(strip, triangles);
        mgPortPrimSubmit(prim, gfx::Primitive::Triangles, triangles);
    }

    prim.End2();
    prim.Shading(0);
    prim.AlphaBlend(MG_ALPHA_BLEND_ADD);
    prim.Begin(MG_PRIM_SPRITE);
    prim.Color(0, 0, 0, 128);
    prim.Vertex(-1, -1, 0);
    prim.Vertex(output_texture->width + 1, output_texture->height + 1, 0);
    prim.End();
    mgSetPkFrameBuffer(-1, -1, -1, -1);
}
