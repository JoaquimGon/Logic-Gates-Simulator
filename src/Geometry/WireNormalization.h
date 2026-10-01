#pragma once
#include "Geometry/GeometryTypes.h"
#include "Geometry/Wire.h"

#include <map>
#include <span>
#include <vector>

struct WireChanges
{
    std::vector<WireId> removed;
    std::vector<WireId> added;
};

/**
 * @brief Normalizes overlaps, branches and degree-two seams in place.
 * @param wires Committed routes; borrowed wire/path references may be invalidated.
 * @param pins Absolute anchors that must remain endpoints and cannot be healed away.
 * @param nextWireId Monotonic allocator; every replacement receives a fresh ID.
 * @return Removed/added IDs relative to the input. Unchanged routes retain their IDs.
 * No nets or simulation edges are built here; callers must rebuild derived connectivity.
 */
WireChanges
normalizeWires(std::map<WireId, Wire>& wires, std::span<const PinAnchor> pins, WireId& nextWireId);

bool collinearOverlap(
    GridCoords a, GridCoords b, GridCoords c, GridCoords d, GridCoords& outStart, GridCoords& outEnd
);
