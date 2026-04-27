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
    // NOTE: parameter order is (top, bottom, left, right) NOT standard GL.
    // Output is the standard column-major ortho:
    //   m[0]  = 2 / (right - left)     X scale
    //   m[5]  = 2 / (top - bottom)     Y scale
    //   m[10] = 1 / (far - near)       Z scale
    //   m[12] = -(right+left)/(R-L)    X centring (col 3, row 0)
    //   m[13] = -(top+bottom)/(T-B)    Y centring (col 3, row 1)
    //   m[14] = near / (near - far)    Z centring
    static void OrthoW(float top, float bottom, float left, float right,
                       float near, float far, float w, Matrix44& out) {
        out.Identity();
        out.m[15] = w;
        float invTB = 1.0f / (top - bottom);
        float invRL = 1.0f / (right - left);
        out.m[10] = 1.0f / (far - near);
        out.m[14] = near / (near - far);
        out.m[12] = -(right + left) * invRL;    // X centring uses R/L
        out.m[13] = -(top + bottom) * invTB;    // Y centring uses T/B
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

    // Matches _Matrix44<float>::Scale44 (0x0012f9a0) — in-place column-scale
    void ApplyScale(float sx, float sy, float sz) {
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

    // Alias matching original _Matrix44<float>::Scale44 static factory
    static Matrix44 Scale44(const Vec3& s) { return MakeScale(s); }
    static Matrix44 Scale44(float sx, float sy, float sz) { return MakeScale(sx, sy, sz); }

    // Static factory: create a pure translation matrix
    static Matrix44 MakeTranslate(const Vec3& t) {
        Matrix44 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    // Matches _Matrix44<float>::RotX44 @ 0x00172f58 — PRE-multiply by Rot_std_X(+alpha).
    // Binary iterates rows 1 and 2 across all columns:
    //   new_row1 = cos*row1 - sin*row2
    //   new_row2 = sin*row1 + cos*row2
    // Linear (col-major m[c*4+r]): per col c, mix m[c*4+1] (row 1) with m[c*4+2] (row 2).
    // (An earlier port version POST-multiplied with sign flips. That was based on
    //  a misread of the binary's row-vs-col iteration -- the binary genuinely
    //  pre-multiplies the matrix by standard CCW rotation.)
    void RotX44(float sinA, float cosA) {
        for (int c = 0; c < 4; c++) {
            float a = m[c * 4 + 1];   // row 1, col c
            float b = m[c * 4 + 2];   // row 2, col c
            m[c * 4 + 1] = cosA * a - sinA * b;
            m[c * 4 + 2] = sinA * a + cosA * b;
        }
    }

    // Matches _Matrix44<float>::RotY44 @ 0x00172fdc — PRE-multiply by Rot_std_Y(+alpha).
    // Binary iterates rows 0 and 2:
    //   new_row0 = cos*row0 + sin*row2
    //   new_row2 = -sin*row0 + cos*row2
    void RotY44(float sinA, float cosA) {
        for (int c = 0; c < 4; c++) {
            float a = m[c * 4 + 0];   // row 0, col c
            float b = m[c * 4 + 2];   // row 2, col c
            m[c * 4 + 0] =  cosA * a + sinA * b;
            m[c * 4 + 2] = -sinA * a + cosA * b;
        }
    }

    // Matches _Matrix44<float>::RotZ44 @ 0x00144958 — PRE-multiply by Rot_std_Z(+alpha).
    // Binary iterates rows 0 and 1:
    //   new_row0 = cos*row0 - sin*row1
    //   new_row1 = sin*row0 + cos*row1
    void RotZ44(float sinA, float cosA) {
        for (int c = 0; c < 4; c++) {
            float a = m[c * 4 + 0];   // row 0, col c
            float b = m[c * 4 + 1];   // row 1, col c
            m[c * 4 + 0] = cosA * a - sinA * b;
            m[c * 4 + 1] = sinA * a + cosA * b;
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
