#pragma once
#include "raylib.h"
#include <algorithm>
#include <cmath>

// Small 2D helpers shared by simulation and rendering.

inline Vector2 vadd(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 vsub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 vscale(Vector2 a, float s) { return {a.x * s, a.y * s}; }
inline float vlen(Vector2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
inline float vdist(Vector2 a, Vector2 b) { return vlen(vsub(a, b)); }
inline Vector2 vperp(Vector2 a) { return {-a.y, a.x}; }

inline Vector2 vnorm(Vector2 a) {
    float l = vlen(a);
    return (l < 0.0001f) ? Vector2{0, 0} : Vector2{a.x / l, a.y / l};
}
inline Vector2 vpolar(float ang, float len) { return {cosf(ang) * len, sinf(ang) * len}; }

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

// Shortest distance from p to the segment ab.
inline float distToSeg(Vector2 p, Vector2 a, Vector2 b) {
    Vector2 ab = vsub(b, a), ap = vsub(p, a);
    float len2 = ab.x * ab.x + ab.y * ab.y;
    float t = (len2 < 1e-6f) ? 0.0f : clampf((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f);
    return vdist(p, vadd(a, vscale(ab, t)));
}

// Rotate `from` toward `to` by at most maxRad.
inline Vector2 steerToward(Vector2 from, Vector2 to, float maxRad) {
    float a = atan2f(from.y, from.x), b = atan2f(to.y, to.x);
    float d = b - a;
    while (d > PI) d -= 2 * PI;
    while (d < -PI) d += 2 * PI;
    return vpolar(a + clampf(d, -maxRad, maxRad), vlen(from));
}
