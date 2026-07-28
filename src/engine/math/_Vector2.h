#ifndef MORTAR_VECTOR2_TEMPLATE_H
#define MORTAR_VECTOR2_TEMPLATE_H

// Templated `_Vector2<T>` matching the binary's class name + ABI.
// Mangled as `8_Vector2IfE` at global scope; instantiated with `<float>`.
//
// Same shape as the existing port-side `struct Vec2` but templated under
// the binary's reserved-identifier name so signatures involving
// `_Vector2<float>` mangle identically.

#include <cmath>

template<class T>
struct _Vector2 {
    T x, y;

    _Vector2() : x(0), y(0) {}
    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00117830 (asm-inspector)
    _Vector2(T x_, T y_) : x(x_), y(y_) {}

    _Vector2 operator+(const _Vector2& o) const { return _Vector2(x + o.x, y + o.y); }
    _Vector2 operator-(const _Vector2& o) const { return _Vector2(x - o.x, y - o.y); }
    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x001178c4 (asm-inspector)
    // Scalar by const-ref (vldr from [r2]), not by value.
    _Vector2 operator*(const T& s) const { return _Vector2(x * s, y * s); }
    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x00152664 (asm-inspector)
    // 2 separate vdiv.f32 ops, not reciprocal-multiply (different rounding).
    _Vector2 operator/(const T& s) const { return _Vector2(x / s, y / s); }
    _Vector2 operator-() const { return _Vector2(-x, -y); }

    _Vector2& operator+=(const _Vector2& o) { x += o.x; y += o.y; return *this; }
    _Vector2& operator-=(const _Vector2& o) { x -= o.x; y -= o.y; return *this; }
    _Vector2& operator*=(T s) { x *= s; y *= s; return *this; }
    // ASM-verified: 2026-05-06T00:00 v1.6.1 _Vector2<float>::operator/=(float) @ 0x001d7854 (asm-inspector)
    // Mangled suffix `dVEf` confirms the scalar is passed BY VALUE.
    _Vector2& operator/=(T s) { x /= s; y /= s; return *this; }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x001526a8 (asm-inspector)
    T Dot(const _Vector2& o) const { return x * o.x + y * o.y; }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x001526c4 (asm-inspector)
    T MagnitudeSqr() const { return Dot(*this); }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 _Vector2<float>::Magnitude() const @ 0x001d7870 (asm-inspector)
    T Magnitude() const { return T(std::sqrt((double)MagnitudeSqr())); }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 _Vector2<float>::Normalise() @ 0x001d7894 (asm-inspector)
    // Recursive 1e6-scale retry AND returning the PRE-retry magnitude are both
    // confirmed binary behaviour -- not a port simplification.
    T Normalise() {
        if (x == T(0) && y == T(0)) return T(0);
        T mag = Magnitude();
        if (mag == T(0)) {
            *this *= T(1000000);
            Normalise();
        } else {
            *this /= mag;
        }
        return mag;
    }

    // Static constants matching `_Vector2<float>::Zero` BSS slot.
    static const _Vector2& Zero() { static _Vector2 z(T(0), T(0)); return z; }
    static const _Vector2& One()  { static _Vector2 o(T(1), T(1)); return o; }
};

template<class T>
inline _Vector2<T> operator*(T s, const _Vector2<T>& v) { return v * s; }

#endif // MORTAR_VECTOR2_TEMPLATE_H
