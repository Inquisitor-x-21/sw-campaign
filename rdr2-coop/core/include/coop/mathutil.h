#pragma once

#include <cmath>

namespace coop {

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

inline float length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline float length2d(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline float distance(Vec3 a, Vec3 b) { return length(a - b); }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline bool isFinite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Wraps degrees into [0, 360).
inline float wrapDegrees(float d) {
    d = std::fmod(d, 360.f);
    if (d < 0.f) d += 360.f;
    if (d >= 360.f) d = 0.f;
    return d;
}

// Shortest signed rotation from a to b, in (-180, 180].
inline float angleDelta(float a, float b) {
    float d = wrapDegrees(b - a);
    if (d > 180.f) d -= 360.f;
    return d;
}

inline float lerpAngle(float a, float b, float t) { return wrapDegrees(a + angleDelta(a, b) * t); }

// RAGE headings: 0 = north (+Y), increasing counter-clockwise (90 = west, -X).
inline Vec3 headingToForward(float headingDeg) {
    const float r = headingDeg * 3.14159265358979f / 180.f;
    return {-std::sin(r), std::cos(r), 0.f};
}

inline float forwardToHeading(Vec3 f) {
    return wrapDegrees(std::atan2(-f.x, f.y) * 180.f / 3.14159265358979f);
}

}  // namespace coop
