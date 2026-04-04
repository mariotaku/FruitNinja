#ifndef MORTAR_MATRIX44_H
#define MORTAR_MATRIX44_H

#include "Vec3.h"
#include <cstring>

// Column-major 4x4 matrix matching original _Matrix44<float>
// m[col*4 + row] — OpenGL convention
struct Matrix44 {
    float m[16];

    Matrix44() { Identity(); }

    void Identity() {
        memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    // Matches _Matrix44<float>::OrthoW (0x0019e7a8)
    // NOTE: parameter order is (top, bottom, left, right) NOT standard GL
    static void OrthoW(float top, float bottom, float left, float right,
                       float near, float far, float w, Matrix44& out) {
        out.Identity();
        out.m[15] = w;
        float invTB = 1.0f / (top - bottom);
        float invRL = 1.0f / (right - left);
        out.m[10] = 1.0f / (far - near);
        out.m[14] = near / (near - far);       // out[3][2]
        out.m[13] = -(right + left) * invRL;    // out[3][1]
        out.m[12] = -(top + bottom) * invTB;    // out[3][0]
        out.m[0]  = 2.0f * invRL;               // 2/(right-left)
        out.m[5]  = 2.0f * invTB;               // 2/(top-bottom)
    }

    // Matches _Matrix44<float>::GlobalTranslate44 (0x0012f954)
    // col[3] += (tx, ty, tz) — world-space translate
    void GlobalTranslate44(float tx, float ty, float tz) {
        m[12] += tx;
        m[13] += ty;
        m[14] += tz;
    }

    // Vec3 overload for compatibility
    void GlobalTranslate44(const Vec3& t) {
        GlobalTranslate44(t.x, t.y, t.z);
    }

    // Matches _Matrix44<float>::LocalTranslate44 (0x0019a3d4)
    // col[3] += col[0]*tx + col[1]*ty + col[2]*tz — local-space translate
    void LocalTranslate44(float tx, float ty, float tz) {
        m[12] += m[0] * tx + m[4] * ty + m[8]  * tz;
        m[13] += m[1] * tx + m[5] * ty + m[9]  * tz;
        m[14] += m[2] * tx + m[6] * ty + m[10] * tz;
    }

    // Matches _Matrix44<float>::Scale44 (0x0012f9a0) — column-scale
    void Scale44(float sx, float sy, float sz) {
        m[0]  *= sx; m[1]  *= sx; m[2]  *= sx; m[3]  *= sx;
        m[4]  *= sy; m[5]  *= sy; m[6]  *= sy; m[7]  *= sy;
        m[8]  *= sz; m[9]  *= sz; m[10] *= sz; m[11] *= sz;
    }

    // Static factory: create a pure scale matrix
    static Matrix44 MakeScale(const Vec3& s) {
        Matrix44 r;
        memset(r.m, 0, sizeof(r.m));
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = 1.0f;
        return r;
    }

    static Matrix44 MakeScale(float sx, float sy, float sz) {
        return MakeScale(Vec3(sx, sy, sz));
    }

    // Static factory: create a pure translation matrix
    static Matrix44 MakeTranslate(const Vec3& t) {
        Matrix44 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    // Matches _Matrix44<float>::RotZ44
    void RotZ44(float sinA, float cosA) {
        for (int i = 0; i < 4; i++) {
            float a = m[i];       // col 0
            float b = m[4 + i];   // col 1
            m[i]     = a * cosA + b * sinA;
            m[4 + i] = -a * sinA + b * cosA;
        }
    }

    Matrix44 operator*(const Matrix44& b) const {
        Matrix44 r;
        for (int c = 0; c < 4; c++) {
            for (int row = 0; row < 4; row++) {
                r.m[c * 4 + row] =
                    m[0 * 4 + row] * b.m[c * 4 + 0] +
                    m[1 * 4 + row] * b.m[c * 4 + 1] +
                    m[2 * 4 + row] * b.m[c * 4 + 2] +
                    m[3 * 4 + row] * b.m[c * 4 + 3];
            }
        }
        return r;
    }

    const float* ptr() const { return m; }
    float* ptr() { return m; }
};

#endif
