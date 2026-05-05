#ifndef MORTAR_MATRIX44_TEMPLATE_H
#define MORTAR_MATRIX44_TEMPLATE_H

// Templated `_Matrix44<T>` matching the binary's class name + ABI.
// Mangled as `9_Matrix44IfE` at global scope; instantiated with `<float>`.
//
// Column-major 4x4 matrix; m[col*4 + row], OpenGL convention.
// Body mirrors the existing port-side `struct Matrix44`. Method addresses
// in the binary are noted on each member.

#include "_Vector3.h"
#include <cstring>

template<class T>
struct _Matrix44 {
    T m[16];

    _Matrix44() { Identity(); }

    void Identity() {
        for (int i = 0; i < 16; ++i) m[i] = T(0);
        m[0] = m[5] = m[10] = m[15] = T(1);
    }

    // Matches binary `_Matrix44<float>::OrthoW` @ 0x0019e7a8.
    // NOTE: parameter order is (top, bottom, left, right) NOT standard GL.
    // Output mirrors the standard column-major ortho with adjustable w:
    //   m[0]  = 2 / (right - left)     X scale
    //   m[5]  = 2 / (top - bottom)     Y scale
    //   m[10] = 1 / (far - near)       Z scale
    //   m[12] = -(right+left)/(R-L)    X centring
    //   m[13] = -(top+bottom)/(T-B)    Y centring
    //   m[14] = near / (near - far)    Z centring
    static void OrthoW(T top, T bottom, T left, T right,
                       T near_, T far_, T w, _Matrix44& out) {
        out.Identity();
        out.m[15] = w;
        T invTB = T(1) / (top - bottom);
        T invRL = T(1) / (right - left);
        out.m[10] = T(1) / (far_ - near_);
        out.m[14] = near_ / (near_ - far_);
        out.m[12] = -(right + left) * invRL;
        out.m[13] = -(top + bottom) * invTB;
        out.m[0]  = T(2) * invRL;
        out.m[5]  = T(2) * invTB;
    }

    // Matches binary `_Matrix44<float>::GlobalTranslate44` @ 0x0012f954.
    // col[3] += (tx, ty, tz) -- world-space translate.
    void GlobalTranslate44(T tx, T ty, T tz) {
        m[12] += tx;
        m[13] += ty;
        m[14] += tz;
    }

    void GlobalTranslate44(const _Vector3<T>& t) {
        GlobalTranslate44(t.x, t.y, t.z);
    }

    // Matches binary `_Matrix44<float>::LocalTranslate44` @ 0x0019a3d4.
    // col[3] += col[0]*tx + col[1]*ty + col[2]*tz -- local-space translate.
    void LocalTranslate44(T tx, T ty, T tz) {
        m[12] += m[0] * tx + m[4] * ty + m[8]  * tz;
        m[13] += m[1] * tx + m[5] * ty + m[9]  * tz;
        m[14] += m[2] * tx + m[6] * ty + m[10] * tz;
    }

    // Matches binary `_Matrix44<float>::Scale44` @ 0x0012f9a0 (in-place
    // column-scale; not the same as the static factory below).
    void ApplyScale(T sx, T sy, T sz) {
        m[0]  *= sx; m[1]  *= sx; m[2]  *= sx; m[3]  *= sx;
        m[4]  *= sy; m[5]  *= sy; m[6]  *= sy; m[7]  *= sy;
        m[8]  *= sz; m[9]  *= sz; m[10] *= sz; m[11] *= sz;
    }

    static _Matrix44 MakeScale(const _Vector3<T>& s) {
        _Matrix44 r;
        for (int i = 0; i < 16; ++i) r.m[i] = T(0);
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; r.m[15] = T(1);
        return r;
    }

    static _Matrix44 MakeScale(T sx, T sy, T sz) {
        return MakeScale(_Vector3<T>(sx, sy, sz));
    }

    // Alias matching binary `_Matrix44<float>::Scale44` static factory.
    static _Matrix44 Scale44(const _Vector3<T>& s) { return MakeScale(s); }
    static _Matrix44 Scale44(T sx, T sy, T sz) { return MakeScale(sx, sy, sz); }

    static _Matrix44 MakeTranslate(const _Vector3<T>& t) {
        _Matrix44 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }

    // Matches binary `_Matrix44<float>::RotX44` @ 0x00172f58 -- PRE-multiply
    // by Rot_std_X(+alpha). Per col c, mix m[c*4+1] (row 1) with m[c*4+2]
    // (row 2):
    //   new_row1 = cos*row1 - sin*row2
    //   new_row2 = sin*row1 + cos*row2
    void RotX44(T sinA, T cosA) {
        for (int c = 0; c < 4; c++) {
            T a = m[c * 4 + 1];
            T b = m[c * 4 + 2];
            m[c * 4 + 1] = cosA * a - sinA * b;
            m[c * 4 + 2] = sinA * a + cosA * b;
        }
    }

    // Matches binary `_Matrix44<float>::RotY44` @ 0x00172fdc.
    //   new_row0 = cos*row0 + sin*row2
    //   new_row2 = -sin*row0 + cos*row2
    void RotY44(T sinA, T cosA) {
        for (int c = 0; c < 4; c++) {
            T a = m[c * 4 + 0];
            T b = m[c * 4 + 2];
            m[c * 4 + 0] =  cosA * a + sinA * b;
            m[c * 4 + 2] = -sinA * a + cosA * b;
        }
    }

    // Matches binary `_Matrix44<float>::RotZ44` @ 0x00144958.
    //   new_row0 = cos*row0 - sin*row1
    //   new_row1 = sin*row0 + cos*row1
    void RotZ44(T sinA, T cosA) {
        for (int c = 0; c < 4; c++) {
            T a = m[c * 4 + 0];
            T b = m[c * 4 + 1];
            m[c * 4 + 0] = cosA * a - sinA * b;
            m[c * 4 + 1] = sinA * a + cosA * b;
        }
    }

    _Matrix44 operator*(const _Matrix44& b) const {
        _Matrix44 r;
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

    const T* ptr() const { return m; }
    T* ptr() { return m; }
};

#endif // MORTAR_MATRIX44_TEMPLATE_H
