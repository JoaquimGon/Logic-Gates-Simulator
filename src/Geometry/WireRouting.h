#pragma once

#include "Geometry/GeometryTypes.h"
#include "Geometry/Wire.h"

#include <map>
#include <optional>
#include <span>
#include <vector>

/** Orthogonal routing with one grid cell of clearance around bodies and pins.
 * Only endpoint access may enter its own component's padding. Search is bounded. */
std::optional<std::vector<GridCoords>> routeWire(
    GridCoords start,
    GridCoords end,
    std::span<const ComponentGeometry> components,
    const std::map<WireId, Wire>& wires,
    NetId allowedNet = INVALID_NET_ID,
    bool horizontalFirst = true
);

/** Follows moved pin endpoints while keeping fixed endpoints/branch junctions.
 * Returns false without publishing any routes when no safe path exists. */
bool rerouteMovedWires(
    std::span<const ComponentGeometry> before,
    std::span<const ComponentGeometry> after,
    std::span<const int> moved,
    std::map<WireId, Wire>& wires
);
