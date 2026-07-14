// Vec3 (_Vector3<float>) unit test.
//
// Covers field construction, arithmetic operators, Dot, Magnitude,
// MagnitudeSqr, and Normalise (including the documented zero-vector
// early-return and the recursive near-zero 1M-scale retry path).
//
// Pure in-process: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "math/_Vector3.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

// Absolute tolerance for float comparisons.
static const float kTol = 1e-5f;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

static bool feq(float a, float b, float tol = kTol)
{
    float diff = a - b;
    return diff >= -tol && diff <= tol;
}

#define CHECK_FLOAT(a, b) \
    do { \
        float _a = (float)(a); float _b = (float)(b); \
        if (!feq(_a, _b)) { \
            std::printf("FAIL (%s:%d): %g != %g (tol %g)\n", \
                __FILE__, __LINE__, (double)_a, (double)_b, (double)kTol); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_VEC3(v, ex, ey, ez) \
    do { \
        CHECK_FLOAT((v).x, (ex)); \
        CHECK_FLOAT((v).y, (ey)); \
        CHECK_FLOAT((v).z, (ez)); \
    } while(0)

// ---------------------------------------------------------------------------

static void test_construction()
{
    _Vector3<float> a(1.0f, 2.0f, 3.0f);
    CHECK_FLOAT(a.x, 1.0f);
    CHECK_FLOAT(a.y, 2.0f);
    CHECK_FLOAT(a.z, 3.0f);

    _Vector3<float> b;
    CHECK_FLOAT(b.x, 0.0f);
    CHECK_FLOAT(b.y, 0.0f);
    CHECK_FLOAT(b.z, 0.0f);
}

static void test_add()
{
    _Vector3<float> a(1.0f, 2.0f, 3.0f);
    _Vector3<float> b(4.0f, 5.0f, 6.0f);
    _Vector3<float> c = a + b;
    CHECK_VEC3(c, 5.0f, 7.0f, 9.0f);
}

static void test_sub()
{
    _Vector3<float> a(4.0f, 5.0f, 6.0f);
    _Vector3<float> b(1.0f, 2.0f, 3.0f);
    _Vector3<float> c = a - b;
    CHECK_VEC3(c, 3.0f, 3.0f, 3.0f);
}

static void test_mul_scalar()
{
    _Vector3<float> a(1.0f, 2.0f, 3.0f);
    _Vector3<float> b = a * 3.0f;
    CHECK_VEC3(b, 3.0f, 6.0f, 9.0f);
}

static void test_div_scalar()
{
    _Vector3<float> a(6.0f, 9.0f, 12.0f);
    _Vector3<float> b = a / 3.0f;
    CHECK_VEC3(b, 2.0f, 3.0f, 4.0f);
}

static void test_dot()
{
    // Orthogonal: dot == 0
    _Vector3<float> x(1.0f, 0.0f, 0.0f);
    _Vector3<float> y(0.0f, 1.0f, 0.0f);
    CHECK_FLOAT(x.Dot(y), 0.0f);

    // Known: (2,0,0).(3,0,0) == 6
    _Vector3<float> a(2.0f, 0.0f, 0.0f);
    _Vector3<float> b(3.0f, 0.0f, 0.0f);
    CHECK_FLOAT(a.Dot(b), 6.0f);
}

static void test_magnitude_sqr()
{
    // (3,4,0): 9+16+0 == 25
    _Vector3<float> a(3.0f, 4.0f, 0.0f);
    CHECK_FLOAT(a.MagnitudeSqr(), 25.0f);

    // (1,1,1): 3
    _Vector3<float> b(1.0f, 1.0f, 1.0f);
    CHECK_FLOAT(b.MagnitudeSqr(), 3.0f);

    // zero vector
    _Vector3<float> z(0.0f, 0.0f, 0.0f);
    CHECK_FLOAT(z.MagnitudeSqr(), 0.0f);
}

static void test_magnitude()
{
    // (3,4,0) -> 5
    _Vector3<float> a(3.0f, 4.0f, 0.0f);
    CHECK_FLOAT(a.Magnitude(), 5.0f);

    // zero vector -> 0
    _Vector3<float> z(0.0f, 0.0f, 0.0f);
    CHECK_FLOAT(z.Magnitude(), 0.0f);
}

static void test_normalise_unit()
{
    // (3,4,0) -> (0.6, 0.8, 0); returned mag == 5
    _Vector3<float> a(3.0f, 4.0f, 0.0f);
    float mag = a.Normalise();
    CHECK_FLOAT(mag, 5.0f);
    CHECK_VEC3(a, 0.6f, 0.8f, 0.0f);
    // Post-normalise magnitude is ~1.0
    CHECK_FLOAT(a.Magnitude(), 1.0f);
}

static void test_normalise_zero()
{
    // Documented behavior from _Vector3.h Normalise():
    //   if (x==0 && y==0 && z==0) return T(0);
    // The early explicit zero-check fires before any sqrt or division.
    // The vector is NOT modified; returned magnitude is 0.
    _Vector3<float> z(0.0f, 0.0f, 0.0f);
    float mag = z.Normalise();
    // Must return 0 and must not crash/inf-loop.
    CHECK_FLOAT(mag, 0.0f);
    // Vector stays (0,0,0) -- the early-return leaves it untouched.
    CHECK_VEC3(z, 0.0f, 0.0f, 0.0f);
}

static void test_normalise_near_zero()
{
    // Near-zero input: (1e-20, 1e-20, 1e-20).
    // Magnitude() underflows to 0.0f in float, so the recursive 1M-scale
    // retry path fires. After repeated scaling the value eventually becomes
    // representable; the function terminates and yields a unit vector.
    //
    // We assert:
    //   (a) the call terminates (no inf-loop),
    //   (b) the resulting vector has finite, non-NaN components,
    //   (c) the post-normalise magnitude is approximately 1.0.
    _Vector3<float> v(1.0e-20f, 1.0e-20f, 1.0e-20f);
    v.Normalise();  // return value (pre-scale mag) is not meaningful here
    CHECK(std::isfinite(v.x));
    CHECK(std::isfinite(v.y));
    CHECK(std::isfinite(v.z));
    CHECK_FLOAT(v.Magnitude(), 1.0f);
}

// ---------------------------------------------------------------------------

int main()
{
    std::printf("test_vec3: start\n");

    test_construction();
    std::printf("  construction: OK\n");

    test_add();
    std::printf("  operator+: OK\n");

    test_sub();
    std::printf("  operator-: OK\n");

    test_mul_scalar();
    std::printf("  operator* (scalar): OK\n");

    test_div_scalar();
    std::printf("  operator/ (scalar): OK\n");

    test_dot();
    std::printf("  Dot: OK\n");

    test_magnitude_sqr();
    std::printf("  MagnitudeSqr: OK\n");

    test_magnitude();
    std::printf("  Magnitude: OK\n");

    test_normalise_unit();
    std::printf("  Normalise (3,4,0): OK\n");

    test_normalise_zero();
    std::printf("  Normalise (0,0,0) early-return: OK\n");

    test_normalise_near_zero();
    std::printf("  Normalise near-zero 1M-scale retry: OK\n");

    std::printf("test_vec3: PASS\n");
    return 0;
}
