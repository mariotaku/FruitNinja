#ifndef MORTAR_VECTOR3_TEMPLATE_H
#define MORTAR_VECTOR3_TEMPLATE_H

// Templated `_Vector3<T>` matching the binary's class name + ABI.
// Mangled symbols in FruitNinja.exe reference `8_Vector3IfE` at global
// scope (Itanium ABI: name-length 8, name `_Vector3`, template-arg `<float>`).
//
// `_Vector3<float>` is the binary's instantiation; the existing port-side
// `struct Vec3` (src/engine/math/Vec3.h) is the same 12-byte layout but with
// a non-templated, non-reserved name. This header re-exposes the binary's
// templated form for code that needs ABI-faithful signatures.
//
// Method bodies mirror Vec3's behaviour 1:1 (same operators, Dot, Magnitude,
// Normalise, Cross, Zero/One BSS analogues). Template parameter is generic
// over float/double; the non-trivial sqrt-based methods route through
// std::sqrt so both instantiations work.

#include <cmath>

template<class T>
struct _Vector3 {
    T x, y, z;

    _Vector3() : x(0), y(0), z(0) {}
    _Vector3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

    _Vector3 operator+(const _Vector3& o) const { return _Vector3(x + o.x, y + o.y, z + o.z); }
    _Vector3 operator-(const _Vector3& o) const { return _Vector3(x - o.x, y - o.y, z - o.z); }
    _Vector3 operator*(T s) const { return _Vector3(x * s, y * s, z * s); }
    _Vector3 operator/(T s) const { T inv = T(1) / s; return _Vector3(x * inv, y * inv, z * inv); }
    _Vector3 operator-() const { return _Vector3(-x, -y, -z); }

    _Vector3& operator+=(const _Vector3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    _Vector3& operator-=(const _Vector3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    _Vector3& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }

    // Matches binary `_Vector3<float>::operator/=` @ 0x00138b40.
    _Vector3& operator/=(T s) { x /= s; y /= s; z /= s; return *this; }

    // Component-wise multiply (used in HUDControl3d::Draw).
    _Vector3 operator*(const _Vector3& o) const { return _Vector3(x * o.x, y * o.y, z * o.z); }

    bool operator!=(const _Vector3& o) const { return x != o.x || y != o.y || z != o.z; }
    bool operator==(const _Vector3& o) const { return x == o.x && y == o.y && z == o.z; }

    // Matches binary `_Vector3<float>::Dot` @ 0x00133c4c.
    T Dot(const _Vector3& o) const { return x * o.x + y * o.y + z * o.z; }

    // Matches binary `_Vector3<float>::MagnitudeSqr` @ 0x00133c74.
    T MagnitudeSqr() const { return Dot(*this); }

    // Matches binary `_Vector3<float>::Magnitude` @ 0x00138cdc.
    T Magnitude() const { return T(std::sqrt((double)MagnitudeSqr())); }

    // Matches binary `_Vector3<float>::Normalise` @ 0x00138ce8 -- in-place,
    // returns original magnitude. Recursive 1M-scale retry for near-zero
    // input mirrors the binary's DAT_00138d5c constant.
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

    // Matches binary `_Vector3<float>::Cross` @ 0x0017ea04 -- static, ARM
    // struct-return convention.
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
