#include "radar_math.h"

#include <math.h>

namespace radar {
namespace {

constexpr double EARTH_RADIUS_METERS = 6371000.0;
constexpr double PI_VALUE = 3.14159265358979323846;
constexpr uint32_t RANGES[] = {100, 250, 500, 1000, 2000, 5000, 10000};

double radians(double degrees) { return degrees * PI_VALUE / 180.0; }

}  // namespace

RelativePosition relativePosition(double localLatitude, double localLongitude,
                                  double peerLatitude, double peerLongitude) {
  RelativePosition result;
  const double latitude1 = radians(localLatitude);
  const double latitude2 = radians(peerLatitude);
  const double deltaLatitude = latitude2 - latitude1;
  const double deltaLongitude = radians(peerLongitude - localLongitude);

  const double sinLatitude = sin(deltaLatitude / 2.0);
  const double sinLongitude = sin(deltaLongitude / 2.0);
  const double a = sinLatitude * sinLatitude + cos(latitude1) * cos(latitude2) *
                                                    sinLongitude * sinLongitude;
  const double angularDistance = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  result.distanceMeters = EARTH_RADIUS_METERS * angularDistance;

  const double y = sin(deltaLongitude) * cos(latitude2);
  const double x = cos(latitude1) * sin(latitude2) -
                   sin(latitude1) * cos(latitude2) * cos(deltaLongitude);
  result.bearingDegrees = fmod(atan2(y, x) * 180.0 / PI_VALUE + 360.0, 360.0);
  const double bearing = radians(result.bearingDegrees);
  result.eastMeters = sin(bearing) * result.distanceMeters;
  result.northMeters = cos(bearing) * result.distanceMeters;
  return result;
}

uint32_t selectRangeMeters(double farthestDistanceMeters) {
  for (uint32_t range : RANGES) {
    if (farthestDistanceMeters <= static_cast<double>(range)) return range;
  }
  return RANGES[sizeof(RANGES) / sizeof(RANGES[0]) - 1];
}

PlotPoint project(const RelativePosition &position, uint32_t rangeMeters,
                  int16_t centerX, int16_t centerY, int16_t radiusPixels) {
  PlotPoint result;
  result.x = centerX;
  result.y = centerY;
  if (rangeMeters == 0 || radiusPixels <= 0) return result;

  double scale = static_cast<double>(radiusPixels) / rangeMeters;
  if (position.distanceMeters > rangeMeters && position.distanceMeters > 0.0) {
    scale = static_cast<double>(radiusPixels) / position.distanceMeters;
    result.clamped = true;
  }
  result.x = static_cast<int16_t>(lround(centerX + position.eastMeters * scale));
  result.y = static_cast<int16_t>(lround(centerY - position.northMeters * scale));
  return result;
}

}  // namespace radar
