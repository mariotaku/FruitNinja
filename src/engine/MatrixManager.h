#ifndef FN_MATRIX_MANAGER_H
#define FN_MATRIX_MANAGER_H

#include "Matrix44.h"

// Matches Mortar MatrixStack (at offset +0x1094 in MatrixManager)
struct MatrixStack {
    Matrix44 current;

    void Reset() { current.Identity(); }
    void Scale(const Vec3& s) { current = current * Matrix44::Scale44(s); }
    void Translate(const Vec3& t) { current.GlobalTranslate44(t); }
    void SetCurrentMatrix(const Matrix44& m) { current = m; }
};

// Matches Mortar MatrixManager
// Holds projection + modelview, provides ortho/perspective setup
struct MatrixManager {
    Matrix44 projection;
    Matrix44 view;
    MatrixStack stack;  // at +0x1094 in original

    // Matches SetupOrtho — verified constants: (160, -160, -240, 240, 2000, -6000)
    void SetupOrtho(float left, float right, float bottom, float top,
                    float nearVal, float farVal) {
        Matrix44& p = projection;
        memset(p.m, 0, sizeof(p.m));
        p.m[0]  = 2.0f / (right - left);
        p.m[5]  = 2.0f / (top - bottom);
        p.m[10] = -2.0f / (farVal - nearVal);
        p.m[12] = -(right + left) / (right - left);
        p.m[13] = -(top + bottom) / (top - bottom);
        p.m[14] = -(farVal + nearVal) / (farVal - nearVal);
        p.m[15] = 1.0f;
    }

    // Get the combined projection * view * stack matrix for upload to shader
    Matrix44 GetMVP() const {
        return projection * view * stack.current;
    }

    void UploadCurrentMatrices() {
        // MVP is computed on demand via GetMVP()
    }
};

#endif
