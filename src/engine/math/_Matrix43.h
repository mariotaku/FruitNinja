#ifndef MORTAR_MATRIX43_TEMPLATE_H
#define MORTAR_MATRIX43_TEMPLATE_H

// Templated `_Matrix43<T>` matching the binary's class name + ABI.
// Mangled as `9_Matrix43IfE` at global scope; instantiated with `<float>`.
//
// 4x3 matrix (4 rows, 3 columns) -- a Matrix44 without the 4th column
// (w/perspective). Used for view matrices where col 3 is always (0,0,0,1).
// Body mirrors the existing port-side `struct Matrix43`.

#include "_Vector3.h"
#include "_Matrix44.h"

template<class T>
struct _Matrix43 {
    T data[4][3];   // 48 bytes when T=float: [row][col]

    _Matrix43() {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 3; ++c)
                data[r][c] = T(0);
        data[0][0] = data[1][1] = data[2][2] = T(1);
    }

    // Matches binary `Copy43To44` @ 0x00181cdc -- adds column 3 = (0,0,0,1).
    void ToMatrix44(_Matrix44<T>& out) const {
        out.m[0]  = data[0][0]; out.m[1]  = data[1][0]; out.m[2]  = data[2][0]; out.m[3]  = data[3][0];
        out.m[4]  = data[0][1]; out.m[5]  = data[1][1]; out.m[6]  = data[2][1]; out.m[7]  = data[3][1];
        out.m[8]  = data[0][2]; out.m[9]  = data[1][2]; out.m[10] = data[2][2]; out.m[11] = data[3][2];
        out.m[12] = T(0);       out.m[13] = T(0);       out.m[14] = T(0);       out.m[15] = T(1);
    }

    // Matches binary `Copy44To43` @ 0x00181c68 -- drops column 3 (w).
    static _Matrix43 FromMatrix44(const _Matrix44<T>& in) {
        _Matrix43 out;
        out.data[0][0] = in.m[0];  out.data[0][1] = in.m[4];  out.data[0][2] = in.m[8];
        out.data[1][0] = in.m[1];  out.data[1][1] = in.m[5];  out.data[1][2] = in.m[9];
        out.data[2][0] = in.m[2];  out.data[2][1] = in.m[6];  out.data[2][2] = in.m[10];
        out.data[3][0] = in.m[3];  out.data[3][1] = in.m[7];  out.data[3][2] = in.m[11];
        return out;
    }

    // Matches binary `LookAt43` @ 0x0019e82c -- standard view matrix
    // construction. forward = normalize(target - eye); right = normalize(up x forward);
    // realUp = forward x right.
    static void LookAt43(const _Vector3<T>& eye, const _Vector3<T>& target,
                         const _Vector3<T>& up, _Matrix43& out) {
        _Vector3<T> forward = target - eye;
        forward.Normalise();
        _Vector3<T> right = _Vector3<T>::Cross(up, forward);
        right.Normalise();
        _Vector3<T> realUp = _Vector3<T>::Cross(forward, right);

        out.data[0][0] = right.x;    out.data[0][1] = right.y;    out.data[0][2] = right.z;
        out.data[1][0] = realUp.x;   out.data[1][1] = realUp.y;   out.data[1][2] = realUp.z;
        out.data[2][0] = forward.x;  out.data[2][1] = forward.y;  out.data[2][2] = forward.z;
        out.data[3][0] = -(eye.x * right.x   + eye.y * right.y   + eye.z * right.z);
        out.data[3][1] = -(eye.x * realUp.x  + eye.y * realUp.y  + eye.z * realUp.z);
        out.data[3][2] = -(eye.x * forward.x + eye.y * forward.y + eye.z * forward.z);
    }
};

#endif // MORTAR_MATRIX43_TEMPLATE_H
