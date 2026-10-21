#include "common.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <vector>

#include "asset_directory.hpp"
#include "bounded_text.hpp"
#include "dataread.hpp"
#include "dataread_port.hpp"
#include "effect.hpp"
#include "effectlist.hpp"
#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mg_texture_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"

PORT_OVERRIDE void CFadeInOut::CaptureScreen() {
    if (cross_texture == nullptr || cross_texture->image[0] == nullptr || !mgPortRendererReady()) {
        return;
    }

    // Blit on the GPU: a CPU store followed by a pointer-only texture reload can keep an earlier scene.
    const gfx::TextureHandle target = mgPortRenderTarget(cross_texture, true);
    if (target != gfx::kNullTexture) {
        gfx::BlitTexture(gfx::kPreviousFrame,
                         gfx::Rect{.x = 0, .y = 0, .w = static_cast<int>(gfx::LogicalWidth()), .h = static_cast<int>(gfx::LogicalHeight())},
                         target, gfx::Rect{.x = 0, .y = 0, .w = cross_texture->width, .h = cross_texture->height}, gfx::Filter::Nearest);
    }
}

namespace {

u_int Quadwords(u_int bytes) {
    return bytes % 16 ? bytes / 16 + 1 : bytes / 16;
}

gfx::Vertex2D EdgeVertex(const mgCDrawPrim &prim, float x, float y, const uint8_t rgba[4]) {
    return mgPortPrimVertex(prim, x, y, 0.0f, x, y, rgba, 0);
}

// Corner order matches the builder's own sprite assembly: top-left, top-right, bottom-right, bottom-left.
void AppendSprite(std::vector<gfx::Vertex2D> &out, const mgCDrawPrim &prim, float x0, float y0, float x1, float y1,
                  const uint8_t rgba[4]) {
    out.push_back(EdgeVertex(prim, x0, y0, rgba));
    out.push_back(EdgeVertex(prim, x1, y0, rgba));
    out.push_back(EdgeVertex(prim, x1, y1, rgba));
    out.push_back(EdgeVertex(prim, x0, y1, rgba));
}

} // namespace

PORT_OVERRIDE void DivSpriteScreen(mgCDrawPrim &prim) {
    uint8_t rgba[4];
    mgPortPrimColor(prim, rgba);

    std::vector<gfx::Vertex2D> quads;

    for (int x = 0; x < mgScreenWidth; x += 64) {
        for (int y = 0; y < mgScreenHeight; y += 32) {
            AppendSprite(quads, prim, static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + 64),
                         static_cast<float>(y + 32), rgba);
        }
    }

    mgPortPrimSubmit(prim, gfx::Primitive::Quads, quads);
}

PORT_OVERRIDE void DivSpriteScreen(mgCDrawPrim &prim, int left, int right, int jagged_left) {
    uint8_t rgba[4];
    mgPortPrimColor(prim, rgba);

    const int                  row_height = mgScreenHeight / 16;
    const int                  edge_offset[2] = {-10, 10};
    std::vector<gfx::Vertex2D> strip;

    for (int row = 0; row < 17; row++) {
        const auto  y = static_cast<float>(row * row_height);
        const int   left_x = std::max(0, jagged_left ? left + edge_offset[row % 2] : left);
        const int   right_x = std::max(0, jagged_left ? right : right + edge_offset[row % 2]);
        strip.push_back(EdgeVertex(prim, static_cast<float>(left_x), y, rgba));
        strip.push_back(EdgeVertex(prim, static_cast<float>(right_x), y, rgba));
    }

    std::vector<gfx::Vertex2D> triangles;
    mgPortStripTriangles(strip, triangles);
    mgPortPrimSubmit(prim, gfx::Primitive::Triangles, triangles);
}

PORT_OVERRIDE void CEffectList::LoadEFPFile(char *name, u_int *pack, int block, mgCMemory *stack) {
    int    sizes[64];
    u_int *files[64];
    char  *names[64];

    this->block = block;
    this->pack = pack;
    this->name = reinterpret_cast<char *>(stack->Alloc(Quadwords(strlen(name) + 1)));
    strcpy(this->name, name);

    std::vector<u_int *> texture_documents(AssetDirectoryCount(pack));
    const int            texture_count = AssetDirectoryReadExtension(pack, "texture", texture_documents.data(),
                                                                     static_cast<int>(texture_documents.size()), nullptr, nullptr);
    for (int index = 0; index < texture_count; ++index) {
        if (const auto path = DataReadBufferPath(texture_documents[index])) {
            MgTextureEnterDocument(&mgTexManager, *path, block, nullptr, nullptr);
        }
    }

    effect_num = AssetDirectoryReadExtension(pack, "em", files, 64, sizes, names);
    managers = new (stack->Alloc(Quadwords(effect_num * sizeof(CEffectManager)) + 2)) CEffectManager[effect_num];
    sprites = new (stack->Alloc(Quadwords(effect_num * sizeof(mgC3DSprite)) + 2)) mgC3DSprite[effect_num];

    for (int i = 0; i < effect_num; i++) {
        int particle_num = 0;
        int ctrl_num = 0;
        // The manager is named by the entry's base name, without its directory or extension.
        std::string_view basename = bounded_text::View(names[i]);
        basename.remove_prefix(std::min(basename.size(), basename.find_last_of("/\\") + 1));
        basename = basename.substr(0, basename.find_last_of('.'));
        auto *data = reinterpret_cast<char *>(files[i]);
        managers[i].GetBufferNums(data, sizes[i], &particle_num, &ctrl_num);
        auto *particles = new (stack->Alloc(Quadwords(particle_num * sizeof(CEffect)) + 2)) CEffect[particle_num];
        auto *ctrls = new (stack->Alloc(Quadwords(ctrl_num * sizeof(CEffectCtrl)) + 2)) CEffectCtrl[ctrl_num];
        managers[i].EntryEffCtrls(particles, particle_num, ctrls, ctrl_num);
        managers[i].Initialize();
        managers[i].Load(data, sizes[i]);
        bounded_text::Copy(managers[i].name, basename);
        managers[i].Run();
    }
}
