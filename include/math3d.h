#ifndef MATH3D_H
#define MATH3D_H

#include <cmath>
#include <cstring>

// Column-major 4x4 matrix (OpenGL convention)
// m[col*4 + row]

inline void mat4_identity(float* m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

inline void mat4_multiply(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            tmp[c * 4 + r] =
                a[0 * 4 + r] * b[c * 4 + 0] +
                a[1 * 4 + r] * b[c * 4 + 1] +
                a[2 * 4 + r] * b[c * 4 + 2] +
                a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    memcpy(out, tmp, 16 * sizeof(float));
}

inline void mat4_perspective(float* m, float fov_rad, float aspect, float near, float far) {
    memset(m, 0, 16 * sizeof(float));
    float f = 1.0f / tanf(fov_rad / 2.0f);
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

inline void mat4_look_at(float* m, float ex, float ey, float ez,
                         float cx, float cy, float cz,
                         float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    fx /= fl; fy /= fl; fz /= fl;

    // s = f x u
    float sx = fy*uz - fz*uy;
    float sy = fz*ux - fx*uz;
    float sz = fx*uy - fy*ux;
    float sl = sqrtf(sx*sx + sy*sy + sz*sz);
    sx /= sl; sy /= sl; sz /= sl;

    // u = s x f
    float ux2 = sy*fz - sz*fy;
    float uy2 = sz*fx - sx*fz;
    float uz2 = sx*fy - sy*fx;

    mat4_identity(m);
    m[0] = sx;  m[4] = sy;  m[8]  = sz;
    m[1] = ux2; m[5] = uy2; m[9]  = uz2;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[12] = -(sx*ex + sy*ey + sz*ez);
    m[13] = -(ux2*ex + uy2*ey + uz2*ez);
    m[14] = (fx*ex + fy*ey + fz*ez);
}

inline void mat4_translate(float* m, float x, float y, float z) {
    mat4_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}

inline void mat4_scale(float* m, float x, float y, float z) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = x; m[5] = y; m[10] = z; m[15] = 1.0f;
}

inline void mat4_rotate_x(float* m, float rad) {
    mat4_identity(m);
    float c = cosf(rad), s = sinf(rad);
    m[5] = c;  m[9]  = -s;
    m[6] = s;  m[10] = c;
}

inline void mat4_rotate_y(float* m, float rad) {
    mat4_identity(m);
    float c = cosf(rad), s = sinf(rad);
    m[0] = c;  m[8]  = s;
    m[2] = -s; m[10] = c;
}

inline void mat4_rotate_z(float* m, float rad) {
    mat4_identity(m);
    float c = cosf(rad), s = sinf(rad);
    m[0] = c;  m[4] = -s;
    m[1] = s;  m[5] = c;
}

#endif
