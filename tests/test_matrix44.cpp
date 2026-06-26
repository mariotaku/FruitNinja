// Matrix44 unit test -- pure CPU, no GL, no audio, no SDL.
//
// Convention verified from _Matrix44.h header comment:
//   Column-major 4x4: m[col*4 + row], OpenGL convention.
// Transform a point p with homogeneous w=1:
//   x' = m[0]*px + m[4]*py + m[8]*pz  + m[12]
//   y' = m[1]*px + m[5]*py + m[9]*pz  + m[13]
//   z' = m[2]*px + m[6]*py + m[10]*pz + m[14]
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "math/Matrix44.h"
#include <cstdio>
#include <cmath>

static int g_failures = 0;

#define TEST(name) std::printf("  %-60s", name)
#define PASS()     std::printf("PASS\n")
#define FAIL(msg)  do { std::printf("FAIL: %s\n", msg); ++g_failures; return; } while (0)
#define CHECK(c,m) do { if (!(c)) { FAIL(m); } } while (0)

static const float TOL = 1e-5f;

static bool feq(float a, float b)
{
    float d = a - b;
    return (d < 0 ? -d : d) <= TOL;
}

// Column-major transform: M * (x,y,z,1) -> (x',y',z')
static void xform(const Matrix44& M, float x, float y, float z,
                  float& ox, float& oy, float& oz)
{
    const float* m = M.ptr();
    ox = m[0]*x + m[4]*y + m[8]*z  + m[12];
    oy = m[1]*x + m[5]*y + m[9]*z  + m[13];
    oz = m[2]*x + m[6]*y + m[10]*z + m[14];
}

// ---------------------------------------------------------------------------

static void test_identity()
{
    TEST("Identity: diagonal==1, off-diagonal==0");
    Matrix44 I;
    const float* m = I.ptr();
    // diagonal: m[0], m[5], m[10], m[15]
    CHECK(feq(m[0],  1.0f), "m[0]  != 1");
    CHECK(feq(m[5],  1.0f), "m[5]  != 1");
    CHECK(feq(m[10], 1.0f), "m[10] != 1");
    CHECK(feq(m[15], 1.0f), "m[15] != 1");
    // all off-diagonal elements must be 0
    for (int i = 0; i < 16; ++i) {
        if (i == 0 || i == 5 || i == 10 || i == 15) continue;
        if (!feq(m[i], 0.0f)) {
            std::printf("FAIL: off-diagonal m[%d] = %f\n", i, (double)m[i]);
            ++g_failures;
            return;
        }
    }
    PASS();
}

static void test_multiply_identity()
{
    TEST("M * Identity == M and Identity * M == M");
    Matrix44 I;
    // Build an arbitrary non-identity matrix via MakeScale + GlobalTranslate44
    Matrix44 M = Matrix44::MakeScale(2.0f, 3.0f, 4.0f);
    M.GlobalTranslate44(5.0f, 6.0f, 7.0f);

    Matrix44 MI = M * I;
    Matrix44 IM = I * M;

    const float* pm  = M.ptr();
    const float* pMI = MI.ptr();
    const float* pIM = IM.ptr();

    for (int i = 0; i < 16; ++i) {
        if (!feq(pm[i], pMI[i])) {
            std::printf("FAIL: (M*I)[%d]=%f != M[%d]=%f\n",
                        i, (double)pMI[i], i, (double)pm[i]);
            ++g_failures;
            return;
        }
        if (!feq(pm[i], pIM[i])) {
            std::printf("FAIL: (I*M)[%d]=%f != M[%d]=%f\n",
                        i, (double)pIM[i], i, (double)pm[i]);
            ++g_failures;
            return;
        }
    }
    PASS();
}

static void test_make_scale_transforms_point()
{
    TEST("MakeScale(2,3,4): transform (1,1,1) -> (2,3,4)");
    Matrix44 S = Matrix44::MakeScale(2.0f, 3.0f, 4.0f);
    float ox, oy, oz;
    xform(S, 1.0f, 1.0f, 1.0f, ox, oy, oz);
    CHECK(feq(ox, 2.0f), "x != 2");
    CHECK(feq(oy, 3.0f), "y != 3");
    CHECK(feq(oz, 4.0f), "z != 4");
    PASS();
}

static void test_make_translate_transforms_origin()
{
    TEST("MakeTranslate(5,6,7): origin (0,0,0) -> (5,6,7)");
    // MakeTranslate starts from Identity (default ctor), sets m[12..14].
    Matrix44 T = Matrix44::MakeTranslate(_Vector3<float>(5.0f, 6.0f, 7.0f));
    float ox, oy, oz;
    xform(T, 0.0f, 0.0f, 0.0f, ox, oy, oz);
    CHECK(feq(ox, 5.0f), "x != 5");
    CHECK(feq(oy, 6.0f), "y != 6");
    CHECK(feq(oz, 7.0f), "z != 7");
    PASS();
}

static void test_global_translate_44_transforms_origin()
{
    TEST("GlobalTranslate44(3,4,5) on Identity: origin -> (3,4,5)");
    Matrix44 M;
    M.GlobalTranslate44(3.0f, 4.0f, 5.0f);
    float ox, oy, oz;
    xform(M, 0.0f, 0.0f, 0.0f, ox, oy, oz);
    CHECK(feq(ox, 3.0f), "x != 3");
    CHECK(feq(oy, 4.0f), "y != 4");
    CHECK(feq(oz, 5.0f), "z != 5");
    PASS();
}

static void test_multiply_noncommutative()
{
    TEST("Scale * Translate != Translate * Scale (non-commutative)");
    Matrix44 S = Matrix44::MakeScale(2.0f, 3.0f, 1.0f);
    Matrix44 T = Matrix44::MakeTranslate(_Vector3<float>(10.0f, 0.0f, 0.0f));

    Matrix44 ST = S * T;
    Matrix44 TS = T * S;

    // For a typical scale+translate, the column-3 (translation part) differs.
    // S*T: translation = scale * T_col3 = (2*10, 3*0, 1*0) = (20, 0, 0)
    // T*S: translation = T_col3 + identity*S_col3 = (10, 0, 0)
    // So m[12] must differ.
    const float* pST = ST.ptr();
    const float* pTS = TS.ptr();
    bool differ = false;
    for (int i = 0; i < 16; ++i) {
        if (!feq(pST[i], pTS[i])) { differ = true; break; }
    }
    CHECK(differ, "S*T == T*S (should be non-commutative)");
    PASS();
}

static void test_rotz44_90deg()
{
    TEST("RotZ44(sin90=1,cos90=0) on Identity: (1,0,0) -> (0,1,0)");
    // RotZ44(sinA, cosA): pre-multiplies the existing matrix by Rot_Z(+alpha).
    // On an identity matrix starting at m_Current = Identity:
    //   new_row0 = cos*row0 - sin*row1
    //   new_row1 = sin*row0 + cos*row1
    // With sin=1, cos=0:
    //   col0: new_m[0]=0*1-1*0=0, new_m[1]=1*1+0*0=1, new_m[2]=0
    //   So (1,0,0) maps to (0, 1, 0).
    Matrix44 M;
    M.RotZ44(1.0f, 0.0f); // sinA=1 (sin 90deg), cosA=0 (cos 90deg)
    float ox, oy, oz;
    xform(M, 1.0f, 0.0f, 0.0f, ox, oy, oz);
    CHECK(feq(ox, 0.0f), "x != 0");
    CHECK(feq(oy, 1.0f), "y != 1");
    CHECK(feq(oz, 0.0f), "z != 0");
    PASS();
}

static void test_rotz44_minus90deg()
{
    TEST("RotZ44(sin=-1,cos=0) on Identity: (0,1,0) -> (1,0,0)");
    // Rotate (0,1,0) by -90deg: sin=-1, cos=0
    //   col1: new_m[4]=0*0-(-1)*1=1, new_m[5]=(-1)*0+0*1=0
    // So (0,1,0) -> (1,0,0).
    Matrix44 M;
    M.RotZ44(-1.0f, 0.0f);
    float ox, oy, oz;
    xform(M, 0.0f, 1.0f, 0.0f, ox, oy, oz);
    CHECK(feq(ox, 1.0f), "x != 1");
    CHECK(feq(oy, 0.0f), "y != 0");
    CHECK(feq(oz, 0.0f), "z != 0");
    PASS();
}

static void test_apply_scale()
{
    TEST("ApplyScale(2,3,4) on Identity: transform (1,1,1) -> (2,3,4)");
    Matrix44 M;
    M.ApplyScale(2.0f, 3.0f, 4.0f);
    float ox, oy, oz;
    xform(M, 1.0f, 1.0f, 1.0f, ox, oy, oz);
    CHECK(feq(ox, 2.0f), "x != 2");
    CHECK(feq(oy, 3.0f), "y != 3");
    CHECK(feq(oz, 4.0f), "z != 4");
    PASS();
}

static void test_scale44_alias()
{
    TEST("Scale44 alias matches MakeScale");
    Matrix44 A = Matrix44::MakeScale(2.0f, 5.0f, 3.0f);
    Matrix44 B = Matrix44::Scale44(2.0f, 5.0f, 3.0f);
    const float* pa = A.ptr();
    const float* pb = B.ptr();
    for (int i = 0; i < 16; ++i) {
        if (!feq(pa[i], pb[i])) {
            std::printf("FAIL: Scale44 vs MakeScale differ at [%d]: %f vs %f\n",
                        i, (double)pb[i], (double)pa[i]);
            ++g_failures;
            return;
        }
    }
    PASS();
}

static void test_scale_translate_composition()
{
    TEST("Scale(2,3,4) then GlobalTranslate44(10,20,30): (1,1,1) -> (12,23,34)");
    // Scale first (diagonal), then translate (adds to col3).
    // MakeScale(2,3,4): m[0]=2, m[5]=3, m[10]=4, rest identity.
    // GlobalTranslate44(10,20,30): m[12]+=10, m[13]+=20, m[14]+=30.
    // xform (1,1,1): x'=2*1+10=12, y'=3*1+20=23, z'=4*1+30=34.
    Matrix44 M = Matrix44::MakeScale(2.0f, 3.0f, 4.0f);
    M.GlobalTranslate44(10.0f, 20.0f, 30.0f);
    float ox, oy, oz;
    xform(M, 1.0f, 1.0f, 1.0f, ox, oy, oz);
    CHECK(feq(ox, 12.0f), "x != 12");
    CHECK(feq(oy, 23.0f), "y != 23");
    CHECK(feq(oz, 34.0f), "z != 34");
    PASS();
}

static void test_ptr_access()
{
    TEST("ptr() returns contiguous float array of 16 elements");
    Matrix44 M = Matrix44::MakeScale(7.0f, 8.0f, 9.0f);
    const float* p = M.ptr();
    // Diagonal entries for a scale matrix
    CHECK(feq(p[0],  7.0f), "p[0]  != 7");
    CHECK(feq(p[5],  8.0f), "p[5]  != 8");
    CHECK(feq(p[10], 9.0f), "p[10] != 9");
    CHECK(feq(p[15], 1.0f), "p[15] != 1");
    PASS();
}

// ---------------------------------------------------------------------------

int main()
{
    std::printf("test_matrix44:\n");

    test_identity();
    test_multiply_identity();
    test_make_scale_transforms_point();
    test_make_translate_transforms_origin();
    test_global_translate_44_transforms_origin();
    test_multiply_noncommutative();
    test_rotz44_90deg();
    test_rotz44_minus90deg();
    test_apply_scale();
    test_scale44_alias();
    test_scale_translate_composition();
    test_ptr_access();

    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
