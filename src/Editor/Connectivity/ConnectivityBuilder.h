#pragma once
#include "Geometry/GeometryTypes.h"
#include "Geometry/Wire.h"
#include "Simulation/Circuit.h"
#include "Simulation/Net.h"

#include <map>
#include <span>
#include <tuple>
#include <vector>

struct RejectedConnection
{
    NetId netId;
    Connection connection;
    ConnectionResult reason;
};

using PinNetIndex = std::map<std::tuple<int, int, bool>, NetId>;

struct ConnectivityResult
{
    std::map<NetId, Net> nets;
    PinNetIndex pinNets;
    std::map<WireId, NetId> wireNets;
    std::vector<RejectedConnection> rejectedConnections;
    SimulationResult status = SimulationResult::OK;
    NetId nextNetId = 0;
};

/**
 * @brief Derives nets/indexes and replaces Circuit edges from normalized committed geometry.
 * @param wires Read-only routes; intersections connect only through shared endpoints.
 * @param pins Absolute indexed interfaces; direction separates input/output index spaces.
 * @param circuit Receives the complete edge set, or no edges when any edge is rejected.
 * @param nextNetId First unused net ID. Net identities are regenerated on every build.
 * @return Derived topology, diagnostics and advanced allocator; routes remain unchanged.
 * Shorted nets remain undriven, preserving the existing separate short-circuit policy.
 */
ConnectivityResult buildConnectivity(
    const std::map<WireId, Wire>& wires,
    std::span<const PinAnchor> pins,
    Circuit& circuit,
    NetId nextNetId
);
