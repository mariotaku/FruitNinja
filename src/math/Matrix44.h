#ifndef FN_MATRIX44_H
#define FN_MATRIX44_H

#include "Vec3.h"
#include <cstring>

// Column-major 4x4 matrix matching original _Matrix44<float>
// m[col][row] — OpenGL convention: m[col*4 + row]
struct Matrix44 {
    float m[16];

    Matrix44() { Identity(); }

    void Identity() {
        memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    // Matches _Matrix44<float>::Scale44
    static Matrix44 Scale44(const Vec3& s) {
        Matrix44 r;
        memset(r.m, 0, sizeof(r.m));
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = 1.0f;
        return r;
    }

    static Matrix44 Scale44(float sx, float sy, float sz) {
        return Scale44(Vec3(sx, sy, sz));
    }

    // Matches _Matrix44<float>::Translate44
    static Matrix44 Translate44(const Vec3& t) {
        Matrix44 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    // Matches _Matrix44<float>::GlobalTranslate44 — post-multiply translate
    void GlobalTranslate44(const Vec3& t) {
        m[12] += m[0] * t.x + m[4] * t.y + m[8]  * t.z;
        m[13] += m[1] * t.x + m[5] * t.y + m[9]  * t.z;
        m[14] += m[2] * t.x + m[6] * t.y + m[10] * t.z;
    }

    // Matches _Matrix44<float>::RotZ44
    void RotZ44(float sinA, float cosA) {
        // Apply rotation to existing matrix columns 0 and 1
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
};

#endif
