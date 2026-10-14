#pragma once

#include "Geometry/GeometryTypes.h"
#include "Geometry/Wire.h"

#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

/** Orthogonal routing with half a grid cell of clearance around bodies and pins.
 * Only endpoint access may enter its own component's padding. Interior wire crossings
 * are allowed; unrelated endpoints and collinear overlaps are avoided. Search is bounded. */
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
    std::map<WireId, Wire>& wires,
    std::span<const WireId> translatedWires = {}
);

/** Trims simple body-side dangling tails at landing pins and reroutes covered continuations.
 * Applies to moved or newly placed components. Pins, junctions and loops are preserved;
 * failure leaves wires and allocation unchanged. */
bool fitWiresToComponents(
    std::span<const ComponentGeometry> components,
    std::span<const int> placed,
    std::map<WireId, Wire>& wires,
    WireId& nextWireId,
    std::span<const WireId> translatedWires = {}
);

/** Follows explicit endpoint changes when a saved subcircuit's interface changes. */
bool rerouteWireEndpoints(
    std::span<const std::pair<GridCoords, GridCoords>> moves,
    std::span<const ComponentGeometry> components,
    std::map<WireId, Wire>& wires,
    std::span<const WireId> translatedWires = {}
);
