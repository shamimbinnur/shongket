#pragma once

#include <stdint.h>

namespace radar {

struct RelativePosition {
  double distanceMeters = 0.0;
  double bearingDegrees = 0.0;
  double eastMeters = 0.0;
  double northMeters = 0.0;
};

struct PlotPoint {
  int16_t x = 0;
  int16_t y = 0;
  bool clamped = false;
};

RelativePosition relativePosition(double localLatitude, double localLongitude,
                                  double peerLatitude, double peerLongitude);
uint32_t selectRangeMeters(double farthestDistanceMeters);
PlotPoint project(const RelativePosition &position, uint32_t rangeMeters,
                  int16_t centerX, int16_t centerY, int16_t radiusPixels);

}  // namespace radar
