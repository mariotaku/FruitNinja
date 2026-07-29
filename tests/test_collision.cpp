// Collision primitives unit test -- ColSphere / ColLine intersection + penetration.
//
// Tests the double-dispatch ColSphereSphere / ColSphereLine statics and the
// Intersects / IntersectsLine predicates directly, with hand-verifiable geometry.
//
// API conventions verified from source:
//   ColSphere::Intersects        -- uses <= (touching at boundary -> true)
//   ColSphereLine(self,line,out) -- INFINITE-line via Math::ClosestPointOnLine @ 0x0027542c;
//                                    uses < (touching at boundary -> NOT a hit via this path)
//   ColSphereSphere(self,other,out):
//       delta = self.center - other.center  (self-to-other direction REVERSED)
//       push = d - radSum  (negative when overlapping)
//       out = delta/d * push  (points FROM other TOWARD self, scaled by negative overlap)
//       test: distSq < radSum^2  (strictly less-than -> boundary NOT a hit via this path)
//   ColSphereLine(self,line,out):
//       test: distSq < radius^2  (strictly less-than -> boundary NOT a hit via this path)
//
// Pure in-process: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "collision/ColSphere.h"
#include "collision/ColLine.h"
#include "collision/ColAABB.h"
#include "math/_Vector3.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_NEAR(a, b, tol) \
    do { \
        float _a = (float)(a); float _b = (float)(b); float _d = _a - _b; \
        if (_d < 0.0f) _d = -_d; \
        if (_d > (float)(tol)) { \
            std::printf("FAIL (%s:%d): |%g - %g| = %g > tol %g\n", \
                __FILE__, __LINE__, (double)_a, (double)_b, (double)_d, (double)(tol)); \
            ::exit(1); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// ColSphere::Intersects  (predicate, uses <=)
// ---------------------------------------------------------------------------

static void test_sphere_sphere_touching()
{
    // (0,0,0) r=1 and (2,0,0) r=1: radSum=2, dist=2, distSq==radSum^2.
    // Intersects uses <=, so boundary is TRUE.
    ColSphere a(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColSphere b(_Vector3<float>(2.0f, 0.0f, 0.0f), 1.0f);
    CHECK(a.Intersects(b));
    CHECK(b.Intersects(a));
}

static void test_sphere_sphere_no_overlap()
{
    // (0,0,0) r=1 and (3,0,0) r=1: dist=3 > radSum=2.
    ColSphere a(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColSphere b(_Vector3<float>(3.0f, 0.0f, 0.0f), 1.0f);
    CHECK(!a.Intersects(b));
    CHECK(!b.Intersects(a));
}

static void test_sphere_sphere_overlap()
{
    // (0,0,0) r=1 and (1,0,0) r=1: dist=1, radSum=2, overlap=1.
    // Intersects -> true.
    ColSphere a(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColSphere b(_Vector3<float>(1.0f, 0.0f, 0.0f), 1.0f);
    CHECK(a.Intersects(b));
    CHECK(b.Intersects(a));
}

// ---------------------------------------------------------------------------
// ColSphereSphere static helper -- penetration vector convention
//
// delta = self.center - other.center = (0,0,0)-(1,0,0) = (-1,0,0)
// distSq = 1 < radSum^2 = 4 -> hit
// d = 1; push = d - radSum = 1-2 = -1
// delta /= d  -> (-1,0,0) / 1 = (-1,0,0)
// out = delta * push = (-1,0,0) * (-1) = (1,0,0)
//
// So out.x == +1 (pointing from other TOWARD self, magnitude = overlap depth 1).
// ---------------------------------------------------------------------------

static void test_colspheresphere_penetration()
{
    ColSphere self(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColSphere other(_Vector3<float>(1.0f, 0.0f, 0.0f), 1.0f);
    _Vector3<float> out;
    int hit = ColSphere::ColSphereSphere(&self, &other, &out);
    CHECK(hit == 1);
    // Depth (magnitude of out) == push == |d - radSum| = |1-2| = 1.
    float mag = std::sqrt(out.x*out.x + out.y*out.y + out.z*out.z);
    CHECK_NEAR(mag, 1.0f, 1e-5f);
    // Normal points along +X (from other toward self is -(1,0,0) direction scaled by negative push;
    // result is (+1, 0, 0) -- see formula above).
    CHECK_NEAR(out.x, 1.0f, 1e-5f);
    CHECK_NEAR(out.y, 0.0f, 1e-5f);
    CHECK_NEAR(out.z, 0.0f, 1e-5f);
}

// ColSphereSphere uses strictly-less-than: boundary (touching) is NOT a hit via this path.
static void test_colspheresphere_touching_no_hit()
{
    // dist = radSum exactly -> distSq == radSum^2 -> NOT < -> returns 0.
    ColSphere self(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColSphere other(_Vector3<float>(2.0f, 0.0f, 0.0f), 1.0f);
    _Vector3<float> out;
    int hit = ColSphere::ColSphereSphere(&self, &other, &out);
    CHECK(hit == 0);
}

static void test_colspheresphere_no_overlap()
{
    ColSphere self(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColSphere other(_Vector3<float>(3.0f, 0.0f, 0.0f), 1.0f);
    _Vector3<float> out;
    int hit = ColSphere::ColSphereSphere(&self, &other, &out);
    CHECK(hit == 0);
}

// ---------------------------------------------------------------------------
// Degenerate: zero-radius sphere (point sphere)
// ---------------------------------------------------------------------------

static void test_colspheresphere_zero_radius()
{
    // r=0 at origin vs r=1 at (0,0,0): same centre, dist=0 < radSum=1 -> hit.
    ColSphere self(_Vector3<float>(0.0f, 0.0f, 0.0f), 0.0f);
    ColSphere other(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    _Vector3<float> out;
    int hit = ColSphere::ColSphereSphere(&self, &other, &out);
    CHECK(hit == 1);
}

static void test_colspheresphere_coincident_no_overlap()
{
    // Both r=0 at origin: dist=0, radSum=0, distSq==radSum^2 -> NOT < -> returns 0.
    ColSphere self(_Vector3<float>(0.0f, 0.0f, 0.0f), 0.0f);
    ColSphere other(_Vector3<float>(0.0f, 0.0f, 0.0f), 0.0f);
    _Vector3<float> out;
    int hit = ColSphere::ColSphereSphere(&self, &other, &out);
    CHECK(hit == 0);
}

// ---------------------------------------------------------------------------
// ColSphereLine  (static helper, infinite-line, uses strictly-less-than)
//
// task #134: ColSphere::IntersectsLine (a port-only predicate wrapper with no
// binary symbol) was deleted -- ColSphereLine @0x0025d114 is the real, ported
// primitive. These three cases previously exercised IntersectsLine's <=
// boundary rule; rewritten against ColSphereLine's < rule (see the tangent
// case below, whose expected result flips accordingly).
// ---------------------------------------------------------------------------

static void test_sphere_line_through_centre()
{
    // Sphere (0,0,0) r=1; segment (-2,0,0)->(2,0,0) passes through centre.
    // Closest point = (0,0,0); distSq = 0 < 1 -> hit.
    ColSphere s(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColLine   line(_Vector3<float>(-2.0f, 0.0f, 0.0f), _Vector3<float>(2.0f, 0.0f, 0.0f));
    _Vector3<float> out;
    CHECK(ColSphere::ColSphereLine(&s, &line, &out) == 1);
}

static void test_sphere_line_far()
{
    // Sphere (0,0,0) r=1; horizontal segment (-2,2,0)->(2,2,0): infinite line is y=2.
    // Closest on infinite line to origin = (0,2,0); distSq=4 >= r^2=1 -> no hit.
    ColSphere s(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColLine   line(_Vector3<float>(-2.0f, 2.0f, 0.0f), _Vector3<float>(2.0f, 2.0f, 0.0f));
    _Vector3<float> out;
    CHECK(ColSphere::ColSphereLine(&s, &line, &out) == 0);
}

static void test_sphere_line_tangent()
{
    // Sphere (0,0,0) r=1; horizontal segment at y=1 from x=-2 to x=2.
    // Closest point = (0,1,0); distSq=1 == r^2=1 -> ColSphereLine uses strictly-less-than,
    // so the boundary-touching case is NOT a hit (differs from the deleted IntersectsLine's
    // <=, which called this true).
    ColSphere s(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColLine   line(_Vector3<float>(-2.0f, 1.0f, 0.0f), _Vector3<float>(2.0f, 1.0f, 0.0f));
    _Vector3<float> out;
    CHECK(ColSphere::ColSphereLine(&s, &line, &out) == 0);
}

// ---------------------------------------------------------------------------
// ColSphereLine static helper -- penetration vector
// ColSphereLine uses strictly-less-than: distSq < radius^2.
// Tangent (distSq == r^2) is NOT a hit via this helper (but is via predicate).
// ---------------------------------------------------------------------------

static void test_colsphereline_through_centre()
{
    // Sphere (0,0,0) r=1; segment (-2,0,0)->(2,0,0).
    // closest = (0,0,0); delta = closest - center = (0,0,0); distSq=0 < 1 -> hit.
    // mag=0, push=|r-mag|=1, delta after Normalise is zero-vector (0/0 edge case in
    // the binary: it just normalises a zero vec). The binary normalises before checking,
    // so out = {0,0,0} * 1 = {0,0,0} in this degenerate case. We only assert hit==1.
    ColSphere s(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColLine   line(_Vector3<float>(-2.0f, 0.0f, 0.0f), _Vector3<float>(2.0f, 0.0f, 0.0f));
    _Vector3<float> out;
    int hit = ColSphere::ColSphereLine(&s, &line, &out);
    CHECK(hit == 1);
}

static void test_colsphereline_far()
{
    // ASM-verified: ColSphereLine uses the INFINITE line (perpendicular foot), NOT a
    // segment clamp (Math::ClosestPointOnLine @0x0027542c -- no t-clamp). So the line
    // must stay far from the sphere along its WHOLE infinite extent, not just the segment.
    // Horizontal line y=2: infinite line is y=2; closest to origin = (0,2,0), distSq=4 >= 1 -> no hit.
    // (A vertical line through x=0 would be the y-axis -> closest = origin -> a HIT; see
    //  test_colsphereline_through_centre. That is faithful infinite-line behaviour.)
    ColSphere s(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColLine   line(_Vector3<float>(-2.0f, 2.0f, 0.0f), _Vector3<float>(2.0f, 2.0f, 0.0f));
    _Vector3<float> out;
    int hit = ColSphere::ColSphereLine(&s, &line, &out);
    CHECK(hit == 0);
}

static void test_colsphereline_offset_centre()
{
    // Sphere (0,1,0) r=2; segment (-5,0,0)->(5,0,0).
    // Closest point on segment to (0,1,0) = (0,0,0) (foot of perpendicular, clamped within seg).
    // delta = (0,0,0) - (0,1,0) = (0,-1,0); distSq=1 < r^2=4 -> hit.
    // mag=1, push=|2-1|=1, delta normalised=(0,-1,0), out=(0,-1,0)*1=(0,-1,0).
    ColSphere s(_Vector3<float>(0.0f, 1.0f, 0.0f), 2.0f);
    ColLine   line(_Vector3<float>(-5.0f, 0.0f, 0.0f), _Vector3<float>(5.0f, 0.0f, 0.0f));
    _Vector3<float> out;
    int hit = ColSphere::ColSphereLine(&s, &line, &out);
    CHECK(hit == 1);
    CHECK_NEAR(out.x,  0.0f, 1e-5f);
    CHECK_NEAR(out.y, -1.0f, 1e-5f);
    CHECK_NEAR(out.z,  0.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Helper: read the protected m_CollideFlag via known binary offset (+0x10 in Col).
// The binary layout is: vptr@0x00, m_PrimaryPoint@0x04 (12B), m_CollideFlag@0x10.
// We use a local subclass so the protected field is accessible without a public getter.
// ---------------------------------------------------------------------------

struct ColSphereInspect : public ColSphere {
    ColSphereInspect(_Vector3<float> c, float r) : ColSphere(c, r) {}
    uint8_t flag() const { return m_CollideFlag; }
    void    clearFlag()  { ClearCollideFlag(); }
};

// ---------------------------------------------------------------------------
// ColSphere::Collide double-dispatch (sphere vs sphere)
// Exercises the Collide vtable entry and the flag-setting path.
// ---------------------------------------------------------------------------

static void test_collide_sphere_vs_sphere_hit()
{
    ColSphereInspect a(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColSphereInspect b(_Vector3<float>(1.0f, 0.0f, 0.0f), 1.0f);
    _Vector3<float> norm;
    int hit = a.Collide(&b, &norm);
    CHECK(hit == 1);
    // Both flags must be set after a hit.
    CHECK(a.flag() != 0);
    CHECK(b.flag() != 0);
}

static void test_collide_sphere_vs_sphere_no_hit()
{
    ColSphereInspect a(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColSphereInspect b(_Vector3<float>(3.0f, 0.0f, 0.0f), 1.0f);
    _Vector3<float> norm;
    int hit = a.Collide(&b, &norm);
    CHECK(hit == 0);
    CHECK(a.flag() == 0);
    CHECK(b.flag() == 0);
}

// ---------------------------------------------------------------------------
// ColSphere::Collide double-dispatch (sphere vs line)
// ColLine::Collide delegates to ColSphere::IntersectsLine (predicate) not
// ColSphereLine, so it hits on boundary too.
// ---------------------------------------------------------------------------

static void test_collide_sphere_vs_line_hit()
{
    ColSphereInspect s(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColLine          line(_Vector3<float>(-2.0f, 0.0f, 0.0f), _Vector3<float>(2.0f, 0.0f, 0.0f));
    _Vector3<float> norm;
    int hit = s.Collide(&line, &norm);
    CHECK(hit == 1);
}

static void test_collide_sphere_vs_line_no_hit()
{
    // Collide(line) routes through the INFINITE-line ColSphereLine, so the line must
    // stay far on its whole extent: horizontal y=2 -> closest to origin (0,2,0), no hit.
    ColSphereInspect s(_Vector3<float>(0.0f, 0.0f, 0.0f), 1.0f);
    ColLine          line(_Vector3<float>(-2.0f, 2.0f, 0.0f), _Vector3<float>(2.0f, 2.0f, 0.0f));
    _Vector3<float> norm;
    int hit = s.Collide(&line, &norm);
    CHECK(hit == 0);
}

int main()
{
    std::printf("test_collision: start\n");

    // ColSphere::Intersects (predicate, <= boundary)
    test_sphere_sphere_touching();
    std::printf("  sphere_sphere_touching (Intersects boundary=true): OK\n");

    test_sphere_sphere_no_overlap();
    std::printf("  sphere_sphere_no_overlap: OK\n");

    test_sphere_sphere_overlap();
    std::printf("  sphere_sphere_overlap: OK\n");

    // ColSphereSphere static helper (strictly-less-than, penetration vector)
    test_colspheresphere_penetration();
    std::printf("  ColSphereSphere overlap depth=1 normal=+X: OK\n");

    test_colspheresphere_touching_no_hit();
    std::printf("  ColSphereSphere touching boundary=no-hit (<): OK\n");

    test_colspheresphere_no_overlap();
    std::printf("  ColSphereSphere no overlap: OK\n");

    // Degenerate
    test_colspheresphere_zero_radius();
    std::printf("  ColSphereSphere zero-radius vs r=1 coincident: OK\n");

    test_colspheresphere_coincident_no_overlap();
    std::printf("  ColSphereSphere two zero-radius coincident: OK\n");

    // ColSphereLine (static, infinite-line, strictly-less-than boundary)
    test_sphere_line_through_centre();
    std::printf("  sphere_line_through_centre (ColSphereLine): OK\n");

    test_sphere_line_far();
    std::printf("  sphere_line_far (ColSphereLine infinite-line no-hit): OK\n");

    test_sphere_line_tangent();
    std::printf("  sphere_line_tangent (ColSphereLine boundary=no-hit): OK\n");

    // ColSphereLine static helper (strictly-less-than, penetration vector)
    test_colsphereline_through_centre();
    std::printf("  ColSphereLine through centre hit: OK\n");

    test_colsphereline_far();
    std::printf("  ColSphereLine far no-hit: OK\n");

    test_colsphereline_offset_centre();
    std::printf("  ColSphereLine offset penetration (0,-1,0): OK\n");

    // Collide vtable dispatch
    test_collide_sphere_vs_sphere_hit();
    std::printf("  Collide sphere-vs-sphere hit + flags: OK\n");

    test_collide_sphere_vs_sphere_no_hit();
    std::printf("  Collide sphere-vs-sphere no-hit + flags clear: OK\n");

    test_collide_sphere_vs_line_hit();
    std::printf("  Collide sphere-vs-line hit: OK\n");

    test_collide_sphere_vs_line_no_hit();
    std::printf("  Collide sphere-vs-line no-hit: OK\n");

    std::printf("test_collision: PASS\n");
    return 0;
}
