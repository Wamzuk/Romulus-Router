// geo.hpp - geographic helpers shared across the server.
#pragma once

#include <cmath>
#include <string>
#include <cctype>

namespace geo {

constexpr double EARTH_RADIUS_M = 6371000.0; // mean Earth radius in metres
constexpr double PI = 3.14159265358979323846;

inline double deg2rad(double d) { return d * PI / 180.0; }

// Great-circle distance between two lat/lng points, in metres.
inline double haversineMeters(double lat1, double lng1, double lat2, double lng2) {
    double dLat = deg2rad(lat2 - lat1);
    double dLng = deg2rad(lng2 - lng1);
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(deg2rad(lat1)) * std::cos(deg2rad(lat2)) *
               std::sin(dLng / 2) * std::sin(dLng / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return EARTH_RADIUS_M * c;
}

// The k-d tree and the brute-force scan both operate in a flat projected plane
// rather than on raw lat/lng, so a degree of longitude near the data is not
// unfairly "shorter" than a degree of latitude. We scale longitude by the
// cosine of a reference latitude. Both methods use the SAME projection, so they
// are guaranteed to agree on the nearest node.
struct Projector {
    double cosLat = 1.0;
    explicit Projector(double refLatDeg = 0.0) { cosLat = std::cos(deg2rad(refLatDeg)); }
    double x(double lngDeg) const { return lngDeg * cosLat; }
    double y(double latDeg) const { return latDeg; }
};

// Parse an Overture/OSM maxspeed string into metres per second.
//   "25 mph" -> miles per hour
//   "50"     -> kilometres per hour (a bare number)
// Returns <= 0 when it cannot be parsed.
inline double parseSpeedMps(const std::string& raw) {
    if (raw.empty()) return 0.0;
    // Pull the leading number.
    size_t p = 0;
    while (p < raw.size() && (std::isspace((unsigned char)raw[p]))) p++;
    size_t start = p;
    while (p < raw.size() && (std::isdigit((unsigned char)raw[p]) || raw[p] == '.')) p++;
    if (p == start) return 0.0;
    double value = std::strtod(raw.c_str() + start, nullptr);
    if (value <= 0) return 0.0;

    // Look for a unit suffix.
    std::string unit;
    for (size_t k = p; k < raw.size(); k++) {
        char c = raw[k];
        if (std::isalpha((unsigned char)c)) unit += (char)std::tolower((unsigned char)c);
    }
    if (unit == "mph") return value * 1609.344 / 3600.0; // miles/hour -> m/s
    if (unit == "knots") return value * 1852.0 / 3600.0;
    // Default and explicit "kmh"/"kph" -> km/h.
    return value * 1000.0 / 3600.0;
}

// Fallback driving speeds (m/s) by road class, used only when a road has no
// maxspeed tag. These keep "fastest route" meaningful even on data where few
// roads carry a speed limit.
inline double defaultSpeedMps(const std::string& roadClass) {
    // ~ values in km/h, converted to m/s.
    auto kmh = [](double v) { return v * 1000.0 / 3600.0; };
    if (roadClass == "motorway") return kmh(100);
    if (roadClass == "trunk") return kmh(85);
    if (roadClass == "primary") return kmh(65);
    if (roadClass == "secondary") return kmh(55);
    if (roadClass == "tertiary") return kmh(45);
    if (roadClass == "residential") return kmh(30);
    if (roadClass == "living_street") return kmh(15);
    if (roadClass == "service") return kmh(20);
    if (roadClass == "unclassified") return kmh(40);
    return kmh(40);
}

} // namespace geo
