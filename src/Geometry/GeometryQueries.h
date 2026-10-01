#pragma once
#include "Geometry/GeometryTypes.h"
#include "Geometry/Wire.h"

#include <map>
#include <span>

/** @brief Hits pins before wire endpoints/bodies, then inset component bodies. */
HitResult hitGeometry(
    std::span<const ComponentGeometry> components,
    const std::map<WireId, Wire>& wires,
    float worldX,
    float worldY,
    GridCoords gridPos
);

/**
 * @brief Tests body-interior overlap, identical origins and coincident pin anchors.
 * Body boundaries may touch; wires and pin-to-body contacts do not impose clearance.
 * @return False for a missing component ID.
 */
bool overlapsComponent(std::span<const ComponentGeometry> components, int componentId);

/** @brief Returns degree-three-or-higher endpoints, excluding component pin anchors. */
std::vector<WireJunction>
wireJunctions(const std::map<WireId, Wire>& wires, std::span<const PinAnchor> pins);
