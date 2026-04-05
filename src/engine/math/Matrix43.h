#ifndef MORTAR_MATRIX43_H
#define MORTAR_MATRIX43_H

//
// _Matrix43<float> (48 bytes) — 4×3 matrix (4 rows, 3 columns)
// A Matrix44 without the 4th column (w/perspective).
// Used for view matrices where column 3 is always (0, 0, 0, 1).
// See docs/engine/matrix-manager.md for full layout and conversion functions.
//

#include "Vec3.h"
#include "Matrix44.h"
#include <cstring>
#include <cmath>

struct Matrix43 {
    float data[4][3];  // 48 bytes: [row][col]

    Matrix43() {
        memset(data, 0, sizeof(data));
        data[0][0] = data[1][1] = data[2][2] = 1.0f;
    }

    // Matches Copy43To44 (0x00181cdc)
    // Adds column 3: (0, 0, 0, 1)
    void ToMatrix44(Matrix44& out) const {
        // Row-major Matrix43 → column-major Matrix44
        out.m[0]  = data[0][0]; out.m[1]  = data[1][0]; out.m[2]  = data[2][0]; out.m[3]  = data[3][0];
        out.m[4]  = data[0][1]; out.m[5]  = data[1][1]; out.m[6]  = data[2][1]; out.m[7]  = data[3][1];
        out.m[8]  = data[0][2]; out.m[9]  = data[1][2]; out.m[10] = data[2][2]; out.m[11] = data[3][2];
        out.m[12] = 0.0f;       out.m[13] = 0.0f;       out.m[14] = 0.0f;       out.m[15] = 1.0f;
    }

    // Matches Copy44To43 (0x00181c68)
    // Drops column 3 (w) from each row
    static Matrix43 FromMatrix44(const Matrix44& in) {
        Matrix43 out;
        out.data[0][0] = in.m[0];  out.data[0][1] = in.m[4];  out.data[0][2] = in.m[8];
        out.data[1][0] = in.m[1];  out.data[1][1] = in.m[5];  out.data[1][2] = in.m[9];
        out.data[2][0] = in.m[2];  out.data[2][1] = in.m[6];  out.data[2][2] = in.m[10];
        out.data[3][0] = in.m[3];  out.data[3][1] = in.m[7];  out.data[3][2] = in.m[11];
        return out;
    }

    // Matches LookAt43 (0x0019e82c)
    // Standard view matrix construction
    static void LookAt43(const Vec3& eye, const Vec3& target, const Vec3& up, Matrix43& out) {
        Vec3 forward = (target - eye).normalized();
        Vec3 right = Vec3::cross(up, forward).normalized();
        Vec3 realUp = Vec3::cross(forward, right);

        out.data[0][0] = right.x;    out.data[0][1] = right.y;    out.data[0][2] = right.z;
        out.data[1][0] = realUp.x;   out.data[1][1] = realUp.y;   out.data[1][2] = realUp.z;
        out.data[2][0] = forward.x;  out.data[2][1] = forward.y;  out.data[2][2] = forward.z;
        out.data[3][0] = -(eye.x * right.x + eye.y * right.y + eye.z * right.z);
        out.data[3][1] = -(eye.x * realUp.x + eye.y * realUp.y + eye.z * realUp.z);
        out.data[3][2] = -(eye.x * forward.x + eye.y * forward.y + eye.z * forward.z);
    }
};

#endif
