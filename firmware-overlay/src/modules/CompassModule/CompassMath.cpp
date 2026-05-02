// Captain's Compass — geographic math.
// See docs/tdd-issue-002-captains-compass.md §11.

#include "CompassMath.h"

#include <math.h>

namespace {
constexpr float kEarthRadiusMeters = 6371000.0f;
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
constexpr float kI7ToDeg = 1.0f / 1e7f;

inline float toRadians(int32_t latLonI) {
    return static_cast<float>(latLonI) * kI7ToDeg * kDegToRad;
}
}  // namespace

namespace CompassMath {

float bearing(int32_t fromLat, int32_t fromLon, int32_t toLat, int32_t toLon) {
    const float lat1 = toRadians(fromLat);
    const float lat2 = toRadians(toLat);
    const float dLon = toRadians(toLon - fromLon);

    const float x = sinf(dLon) * cosf(lat2);
    const float y = cosf(lat1) * sinf(lat2) - sinf(lat1) * cosf(lat2) * cosf(dLon);
    float deg = atan2f(x, y) * kRadToDeg;
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

float distanceMeters(int32_t fromLat, int32_t fromLon, int32_t toLat, int32_t toLon) {
    const float lat1 = toRadians(fromLat);
    const float lat2 = toRadians(toLat);
    const float dLat = toRadians(toLat - fromLat);
    const float dLon = toRadians(toLon - fromLon);

    const float sinHalfDLat = sinf(dLat * 0.5f);
    const float sinHalfDLon = sinf(dLon * 0.5f);
    const float a = sinHalfDLat * sinHalfDLat + cosf(lat1) * cosf(lat2) * sinHalfDLon * sinHalfDLon;
    const float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    return kEarthRadiusMeters * c;
}

float headingError(float currentHeadingDeg, float bearingDeg) {
    float err = fmodf(bearingDeg - currentHeadingDeg + 360.0f, 360.0f);
    if (err > 180.0f) err -= 360.0f;
    return err;
}

}  // namespace CompassMath
