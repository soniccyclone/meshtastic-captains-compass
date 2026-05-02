// Captain's Compass — geographic math.
// See docs/tdd-issue-002-captains-compass.md §11.
//
// Pure stateless functions. Inputs are integer lat/lon (WGS84 * 1e7) to
// match GPS hardware output and the protobuf wire convention.

#pragma once

#include <stdint.h>

namespace CompassMath {

// Bearing from (fromLat, fromLon) to (toLat, toLon), degrees [0, 360).
// 0 = north, 90 = east, etc.
float bearing(int32_t fromLat, int32_t fromLon, int32_t toLat, int32_t toLon);

// Great-circle distance in meters (Haversine).
float distanceMeters(int32_t fromLat, int32_t fromLon, int32_t toLat, int32_t toLon);

// Signed heading error in degrees [-180, 180].
// Positive = target is clockwise from current heading (turn right).
float headingError(float currentHeadingDeg, float bearingDeg);

}  // namespace CompassMath
