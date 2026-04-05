#ifndef MORTAR_VEC2_H
#define MORTAR_VEC2_H

#include <cmath>

// Matches original _Vector2<float> (8 bytes: float x, y)
struct Vec2 {
    float x, y;

    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(float s) const { float inv = 1.0f / s; return Vec2(x * inv, y * inv); }
    Vec2 operator-() const { return Vec2(-x, -y); }

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) { x /= s; y /= s; return *this; }

    float Dot(const Vec2& o) const { return x * o.x + y * o.y; }

    // Matches _Vector2<float>::MagnitudeSqr (0x000f6030)
    float MagnitudeSqr() const { return Dot(*this); }

    // Matches _Vector2<float>::Magnitude (0x00173080)
    float Magnitude() const { return sqrtf(MagnitudeSqr()); }

    // Matches _Vector2<float>::Normalise (0x00173098)
    float Normalise() {
        if (x == 0.0f && y == 0.0f) return 0.0f;
        float mag = Magnitude();
        if (mag == 0.0f) {
            *this *= 1000000.0f;
            Normalise();
        } else {
            *this /= mag;
        }
        return mag;
    }

    // Static constants matching _Vector2<float>::Zero from BSS
    static const Vec2& Zero() { static Vec2 z(0.0f, 0.0f); return z; }
    static const Vec2& One()  { static Vec2 o(1.0f, 1.0f); return o; }
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

#endif
