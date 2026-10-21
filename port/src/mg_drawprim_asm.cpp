
#include "common.h"

#include <cstring>

#include "mg_drawprim.hpp"
#include "vu0_lanes.hpp"

namespace {

void StoreLanes(u_long128 *&write, const int lanes[4]) {
    if (write == nullptr) {
        return;
    }

    std::memcpy(write, lanes, sizeof(int) * 4);
    write++;
}

} // namespace

void mgCDrawPrim::Data0(float *data) {
    const int lanes[4] = {vu0::Ftoi(data[0]), vu0::Ftoi(data[1]), vu0::Ftoi(data[2]), vu0::Ftoi(data[3])};
    StoreLanes(write, lanes);
}

void mgCDrawPrim::Data4(float *data) {
    const int lanes[4] = {vu0::Ftoi4(data[0]), vu0::Ftoi4(data[1]), vu0::Ftoi4(data[2]), vu0::Ftoi4(data[3])};
    StoreLanes(write, lanes);
}

void mgCDrawPrim::Vertex(float *pos) {
    Vertex4(vu0::Ftoi4(pos[0]), vu0::Ftoi4(pos[1]), vu0::Ftoi(pos[2]));
}

void mgCDrawPrim::Color(float *color) {
    Color(vu0::Ftoi(color[0]), vu0::Ftoi(color[1]), vu0::Ftoi(color[2]), vu0::Ftoi(color[3]));
}
