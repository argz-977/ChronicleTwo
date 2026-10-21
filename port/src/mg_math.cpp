#include "common.h"

#include <libvu0.h>

#include "mg_math.hpp"
#include "stubs/mg_math.hpp"
#include "vu0_lanes.hpp"

namespace {

// Retail adds vf0.x (+0) to every lane it copies, which turns -0 into +0.
float add_vf0x(float v) { return v + 0.0f; }

constexpr int kLanesXYZ[3] = {0, 1, 2};
constexpr int kLanesXYW[3] = {0, 1, 3};

int clip_box_overlap(const int *lanes, float *max0, float *min0, float *max1, float *min1) {
    for (int i = 0; i < 3; i++) {
        const int l = lanes[i];
        if (vu0::SignSet(max0[l] - min1[l]) || vu0::SignSet(max1[l] - min0[l])) {
            return 0;
        }
    }
    return 1;
}

int clip_box_inside(const int *lanes, float *max0, float *min0, float *max1, float *min1) {
    for (int i = 0; i < 3; i++) {
        const int l = lanes[i];
        if (vu0::SignSet(max1[l] - max0[l]) || vu0::SignSet(min0[l] - min1[l])) {
            return 0;
        }
    }
    return 1;
}

// Accumulated in j order: the VU MAC chain's rounding depends on it.
void mat_mul(float out[4][4], const float a[4][4], const float b[4][4]) {
    float r[4][4];
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 4; k++) {
            float acc = a[i][0] * b[0][k];
            for (int j = 1; j < 4; j++) {
                acc = acc + a[i][j] * b[j][k];
            }
            r[i][k] = acc;
        }
    }
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 4; k++) {
            out[i][k] = r[i][k];
        }
    }
}

void copy_matrix(float dst[4][4], float (*src)[4]) {
    for (int r = 0; r < 4; r++) {
        for (int k = 0; k < 4; k++) {
            dst[r][k] = src[r][k];
        }
    }
}

float squared_sum3(float x, float y, float z) { return (x * x + y * y) + z * z; }

} // namespace

void mgFotI4(int *out, float *in) {
    for (int i = 0; i < 4; i++) {
        out[i] = vu0::Ftoi4(in[i]);
    }
}

void mgCreateBox8(float (*corners)[4], float *max, float *min) {
    const float mx[4] = {max[0], max[1], max[2], max[3]};
    const float mn[4] = {min[0], min[1], min[2], min[3]};
    const float out[8][4] = {
        {mn[0],           mn[1],           mn[2],           mn[3]          },
        {add_vf0x(mx[0]), add_vf0x(mn[1]), add_vf0x(mn[2]), add_vf0x(mn[3])},
        {add_vf0x(mn[0]), add_vf0x(mx[1]), add_vf0x(mn[2]), add_vf0x(mn[3])},
        {add_vf0x(mx[0]), add_vf0x(mx[1]), add_vf0x(mn[2]), add_vf0x(mx[3])},
        {add_vf0x(mn[0]), add_vf0x(mn[1]), add_vf0x(mx[2]), add_vf0x(mn[3])},
        {add_vf0x(mx[0]), add_vf0x(mn[1]), add_vf0x(mx[2]), add_vf0x(mx[3])},
        {add_vf0x(mn[0]), add_vf0x(mx[1]), add_vf0x(mx[2]), add_vf0x(mx[3])},
        {mx[0],           mx[1],           mx[2],           mx[3]          },
    };
    for (int c = 0; c < 8; c++) {
        for (int k = 0; k < 4; k++) {
            corners[c][k] = out[c][k];
        }
    }
}

PORT_OVERRIDE void mgZeroVector(float *vector) {
    // Native class members need only float alignment, not quadword alignment.
    vector[0] = 0.0f;
    vector[1] = 0.0f;
    vector[2] = 0.0f;
    vector[3] = 0.0f;
}

void mgZeroVectorW(float *vector) {
    vector[0] = 0.0f;
    vector[1] = 0.0f;
    vector[2] = 0.0f;
    vector[3] = 1.0f;
}

int mgClipBoxVertex(float *point, float *max, float *min) {
    for (int i = 0; i < 3; i++) {
        if (vu0::SignSet(max[i] - point[i]) || vu0::SignSet(point[i] - min[i])) {
            return 0;
        }
    }
    return 1;
}

int mgClipBox(float *max0, float *min0, float *max1, float *min1) {
    return clip_box_overlap(kLanesXYZ, max0, min0, max1, min1);
}

int mgClipBoxW(float *max0, float *min0, float *max1, float *min1) {
    return clip_box_overlap(kLanesXYW, max0, min0, max1, min1);
}

int mgClipInBox(float *max0, float *min0, float *max1, float *min1) {
    return clip_box_inside(kLanesXYZ, max0, min0, max1, min1);
}

int mgClipInBoxW(float *max0, float *min0, float *max1, float *min1) {
    return clip_box_inside(kLanesXYW, max0, min0, max1, min1);
}

void mgAddVector(float *vector, float *add) {
    for (int i = 0; i < 4; i++) {
        vector[i] = vector[i] + add[i];
    }
}

void mgSubVector(float *vector, float *sub) {
    for (int i = 0; i < 4; i++) {
        vector[i] = vector[i] - sub[i];
    }
}

void mgNormalizeVector(float *out, float *in, float length) {
    sceVu0FVECTOR unit;

    sceVu0Normalize(unit, in);
    sceVu0ScaleVector(out, unit, length);
}

void mgVectorMin(float *min, float *a, float *b) {
    float r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = vu0::Min(a[i], b[i]);
    }
    for (int i = 0; i < 4; i++) {
        min[i] = r[i];
    }
}

void mgVectorMin(float *min, float *a, float *b, float *c, float *d) {
    float r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = vu0::Min(vu0::Min(vu0::Min(a[i], b[i]), c[i]), d[i]);
    }
    for (int i = 0; i < 4; i++) {
        min[i] = r[i];
    }
}

void mgVectorMaxMin(float *max, float *min, float *a, float *b) {
    float hi[4];
    float lo[4];
    for (int i = 0; i < 4; i++) {
        hi[i] = vu0::Max(a[i], b[i]);
        lo[i] = vu0::Min(a[i], b[i]);
    }
    for (int i = 0; i < 4; i++) {
        max[i] = hi[i];
        min[i] = lo[i];
    }
}

void mgVectorMaxMin(float *max, float *min, float *a, float *b, float *c) {
    float hi[4];
    float lo[4];
    for (int i = 0; i < 4; i++) {
        hi[i] = vu0::Max(vu0::Max(a[i], b[i]), c[i]);
        lo[i] = vu0::Min(vu0::Min(a[i], b[i]), c[i]);
    }
    for (int i = 0; i < 4; i++) {
        max[i] = hi[i];
        min[i] = lo[i];
    }
}

void mgVectorMaxMin(float *max, float *min, float *a, float *b, float *c, float *d) {
    float hi[4];
    float lo[4];
    for (int i = 0; i < 4; i++) {
        hi[i] = vu0::Max(vu0::Max(vu0::Max(a[i], b[i]), c[i]), d[i]);
        lo[i] = vu0::Min(vu0::Min(vu0::Min(a[i], b[i]), c[i]), d[i]);
    }
    for (int i = 0; i < 4; i++) {
        max[i] = hi[i];
        min[i] = lo[i];
    }
}

void mgBoxMaxMin(mgVu0FBOX *box, mgVu0FBOX *other) {
    float hi[4];
    float lo[4];
    for (int i = 0; i < 4; i++) {
        const float a = box->max[i];
        const float b = box->min[i];
        hi[i] = vu0::Max(vu0::Max(vu0::Max(a, b), other->max[i]), other->min[i]);
        lo[i] = vu0::Min(vu0::Min(vu0::Min(a, b), other->max[i]), other->min[i]);
    }
    for (int i = 0; i < 4; i++) {
        box->max[i] = hi[i];
        box->min[i] = lo[i];
    }
}

void mgPlaneNormal(float *normal, float *v0, float *v1, float *v2) {
    // Retail stores vf12 whole, but only xyz is computed, so its w is whatever the register
    // held before. That value is not recoverable; normal[3] is left as it was.
    float a[3];
    float b[3];
    for (int i = 0; i < 3; i++) {
        a[i] = v1[i] - v0[i];
        b[i] = v2[i] - v0[i];
    }
    const float n[3] = {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
    for (int i = 0; i < 3; i++) {
        normal[i] = n[i];
    }
}

float mgDistVector(float *vector) {
    return vu0::Sqrt(squared_sum3(vector[0], vector[1], vector[2]));
}

float mgDistVectorXZ(float *vector) {
    return vu0::Sqrt(vector[0] * vector[0] + vector[2] * vector[2]);
}

float mgDistVector2(float *vector) {
    return squared_sum3(vector[0], vector[1], vector[2]);
}

float mgDistVector(float *a, float *b) {
    return vu0::Sqrt(squared_sum3(b[0] - a[0], b[1] - a[1], b[2] - a[2]));
}

float mgDistVectorXZ(float *a, float *b) {
    const float dx = b[0] - a[0];
    const float dz = b[2] - a[2];
    return vu0::Sqrt(dx * dx + dz * dz);
}

float mgDistVector2(float *a, float *b) {
    return squared_sum3(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
}

float mgDistVectorXZ2(float *a, float *b) {
    const float dx = b[0] - a[0];
    const float dz = b[2] - a[2];
    return dx * dx + dz * dz;
}

void mgUnitMatrix(float (*matrix)[4]) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            matrix[row][col] = (row == col) ? 1.0f : 0.0f;
        }
    }
}

void mgZeroMatrix(float (*matrix)[4]) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            matrix[row][col] = 0.0f;
        }
    }
}

void MulMatrix3(float (*matrix)[4], float (*second)[4], float (*third)[4]) {
    float m[4][4];
    float t[4][4];
    copy_matrix(m, matrix);
    mat_mul(t, second, m);
    mat_mul(m, third, t);
    copy_matrix(matrix, m);
}

void mgMulMatrix(float (*product)[4], float (*left_matrix)[4], float (*right_matrix)[4]) {
    // Product is right * left: callers such as mg_drawenv and mg_frame depend on this order.
    float l[4][4];
    float r[4][4];
    copy_matrix(l, left_matrix);
    copy_matrix(r, right_matrix);
    float p[4][4];
    mat_mul(p, r, l);
    copy_matrix(product, p);
}

void mgInversMatrix(float (*inverse)[4], float (*matrix)[4]) {
    float m[4][4];
    copy_matrix(m, matrix);
    const float *a = m[0];
    const float *b = m[1];
    const float *c = m[2];
    const float *d = m[3];

    // Retail's accumulation order; the determinant's rounding depends on it.
    float acc = a[0] * b[1] * c[2];
    acc = acc + c[0] * a[1] * b[2];
    acc = acc + b[0] * c[1] * a[2];
    acc = acc - a[0] * c[1] * b[2];
    acc = acc - b[0] * a[1] * c[2];
    const float det = acc - c[0] * b[1] * a[2];
    const float q = vu0::Div(1.0f, det);

    const float *p[3] = {b, c, a};
    const float *s[3] = {c, a, b};
    float        rows[3][3];
    for (int k = 0; k < 3; k++) {
        rows[0][k] = p[k][1] * s[k][2] - p[k][2] * s[k][1];
        rows[1][k] = p[k][2] * s[k][0] - p[k][0] * s[k][2];
        rows[2][k] = p[k][0] * s[k][1] - p[k][1] * s[k][0];
    }

    float out[4][4];
    for (int r = 0; r < 3; r++) {
        for (int k = 0; k < 3; k++) {
            out[r][k] = vu0::Sat((0.0f + rows[r][k]) * q);
        }
        out[r][3] = vu0::Sat(0.0f * q);
    }

    for (int k = 0; k < 3; k++) {
        float t = vu0::Sat(out[0][k] * d[0]);
        t = vu0::Sat(t + vu0::Sat(out[1][k] * d[1]));
        t = vu0::Sat(t + vu0::Sat(out[2][k] * d[2]));
        out[3][k] = 0.0f - t;
    }
    out[3][3] = 1.0f;

    copy_matrix(inverse, out);
}

void mgApplyMatrixN(float (*out)[4], float (*matrix)[4], float (*in)[4], int count) {
    // Retail's do-while touches the first elements even for count < 1.
    if (count < 1) {
        return;
    }
    float m[4][4];
    copy_matrix(m, matrix);
    for (int i = 0; i < count; i++) {
        const float v[4] = {in[i][0], in[i][1], in[i][2], in[i][3]};
        vu0::Transform(out[i], m, v);
    }
}

void mgApplyMatrixN_MaxMin(float (*out)[4], float (*matrix)[4], float (*in)[4], int count,
                           float *max, float *min) {
    // Retail's do-while touches the first elements even for count < 1.
    if (count < 1) {
        return;
    }
    float m[4][4];
    copy_matrix(m, matrix);

    float v0[4];
    {
        const float v[4] = {in[0][0], in[0][1], in[0][2], in[0][3]};
        vu0::Transform(v0, m, v);
        for (int k = 0; k < 4; k++) {
            out[0][k] = v0[k];
        }
    }
    float hi[4];
    float lo[4];
    for (int k = 0; k < 4; k++) {
        hi[k] = add_vf0x(v0[k]);
        lo[k] = add_vf0x(v0[k]);
    }

    for (int i = 1; i < count; i++) {
        const float v[4] = {in[i][0], in[i][1], in[i][2], in[i][3]};
        float       r[4];
        vu0::Transform(r, m, v);
        for (int k = 0; k < 4; k++) {
            out[i][k] = r[k];
            hi[k] = vu0::Max(hi[k], r[k]);
            lo[k] = vu0::Min(lo[k], r[k]);
        }
    }

    for (int k = 0; k < 4; k++) {
        max[k] = hi[k];
        min[k] = lo[k];
    }
}

void mgVectorMinMaxN(float *max, float *min, float (*vectors)[4], int count) {
    // Retail's do-while touches the first elements even for count < 1.
    if (count < 1) {
        return;
    }
    float hi[4];
    float lo[4];
    for (int k = 0; k < 4; k++) {
        hi[k] = vectors[0][k];
        lo[k] = vectors[0][k];
    }
    for (int i = 1; i < count; i++) {
        for (int k = 0; k < 4; k++) {
            hi[k] = vu0::Max(hi[k], vectors[i][k]);
            lo[k] = vu0::Min(lo[k], vectors[i][k]);
        }
    }
    for (int k = 0; k < 4; k++) {
        max[k] = hi[k];
        min[k] = lo[k];
    }
}
