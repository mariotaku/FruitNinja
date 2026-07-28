#ifndef MORTAR_VECTOR3_TEMPLATE_H
#define MORTAR_VECTOR3_TEMPLATE_H

// Templated `_Vector3<T>` matching the binary's class name + ABI.
// Mangled symbols in FruitNinja.exe reference `8_Vector3IfE` at global
// scope (Itanium ABI: name-length 8, name `_Vector3`, template-arg `<float>`).
//
// `_Vector3<float>` is the binary's instantiation and the port's sole Vec3
// type -- the old non-templated `Vec3` typedef has been removed.
//
// Method bodies mirror the binary's Vec3 behaviour 1:1 (same operators, Dot,
// Magnitude, Normalise, Cross, Zero/One BSS analogues). Template parameter is
// generic over float/double; the non-trivial sqrt-based methods route through
// std::sqrt so both instantiations work.

#include <cmath>

template<class T>
struct _Vector3 {
    T x, y, z;

    _Vector3() : x(0), y(0), z(0) {}
    _Vector3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00117674 (asm-inspector)
    _Vector3 operator+(const _Vector3& o) const { return _Vector3(x + o.x, y + o.y, z + o.z); }
    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x001176cc (asm-inspector)
    _Vector3 operator-(const _Vector3& o) const { return _Vector3(x - o.x, y - o.y, z - o.z); }
    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x001176a4 (asm-inspector)
    // Scalar passed by const-ref in the binary (vldr from [r2]), not by value.
    _Vector3 operator*(const T& s) const { return _Vector3(x * s, y * s, z * s); }
    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x0013ce38 (asm-inspector)
    // 3 separate vdiv.f32 ops, not reciprocal-multiply (different rounding).
    _Vector3 operator/(const T& s) const { return _Vector3(x / s, y / s, z / s); }
    _Vector3 operator-() const { return _Vector3(-x, -y, -z); }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00126518 (asm-inspector)
    _Vector3& operator+=(const _Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00117878 (asm-inspector)
    _Vector3& operator-=(const _Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x0011783c (asm-inspector) -- scalar by value
    _Vector3& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00138b40 (asm-inspector) -- scalar by value
    _Vector3& operator/=(T s) { x /= s; y /= s; z /= s; return *this; }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 _Vector3<float>::operator*(_Vector3 const&) const @ 0x0013617c (asm-inspector)
    // Component-wise multiply (used in HUDControl3d::Draw).
    _Vector3 operator*(const _Vector3& o) const { return _Vector3(x * o.x, y * o.y, z * o.z); }

    bool operator!=(const _Vector3& o) const { return x != o.x || y != o.y || z != o.z; }
    bool operator==(const _Vector3& o) const { return x == o.x && y == o.y && z == o.z; }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00133c4c (asm-inspector)
    T Dot(const _Vector3& o) const { return x * o.x + y * o.y + z * o.z; }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00133c74 (asm-inspector)
    T MagnitudeSqr() const { return Dot(*this); }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00138cdc (asm-inspector)
    // Widens to f64, vsqrt.f64, narrows back -- matches binary.
    T Magnitude() const { return T(std::sqrt((double)MagnitudeSqr())); }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00138ce8 (asm-inspector)
    // In-place, returns original magnitude. Recursive 1M-scale retry for
    // near-zero input mirrors DAT_00138d5c = 0x49742400 (1000000.0f).
    T Normalise() {
        if (x == T(0) && y == T(0) && z == T(0)) return T(0);
        T mag = Magnitude();
        if (mag == T(0)) {
            *this *= T(1000000);
            Normalise();
        } else {
            *this /= mag;
        }
        return mag;
    }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x0017ea04 (asm-inspector)
    // Static, ARM struct-return convention; binary uses vnmls.f32 for the
    // (Sn*Sm - Sd) FMA pattern -- the operand pairing below matches.
    static _Vector3 Cross(const _Vector3& a, const _Vector3& b) {
        return _Vector3(
            a.y * b.z - b.y * a.z,
            b.x * a.z - a.x * b.z,
            a.x * b.y - b.x * a.y
        );
    }

    // Static constants matching `_Vector3<float>::Zero` / `One` BSS slots.
    static const _Vector3& Zero() { static _Vector3 z(T(0), T(0), T(0)); return z; }
    static const _Vector3& One()  { static _Vector3 o(T(1), T(1), T(1)); return o; }
};

template<class T>
inline _Vector3<T> operator*(T s, const _Vector3<T>& v) { return v * s; }

#endif // MORTAR_VECTOR3_TEMPLATE_H
