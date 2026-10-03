#include "Editor/Connectivity/ConnectivityBuilder.h"

#include <iostream>

namespace
{
struct WireUnion
{
    std::map<WireId, WireId> parent;

    void add(WireId id) { parent.emplace(id, id); }

    WireId find(WireId id) const
    {
        auto it = parent.find(id);
        while (it != parent.end() && it->second != id)
        {
            id = it->second;
            it = parent.find(id);
        }
        return id;
    }

    void unite(WireId a, WireId b)
    {
        a = find(a);
        b = find(b);
        if (a != b)
            parent[b] = a;
    }
};
} // namespace

ConnectivityResult buildConnectivity(
    const std::map<WireId, Wire>& wires,
    std::span<const PinAnchor> pins,
    Circuit& circuit,
    NetId nextNetId
)
{
    ConnectivityResult result;
    result.nextNetId = nextNetId;

    struct PinAtPoint
    {
        PinRef pin;
        bool isOutput;
    };

    std::map<std::pair<int, int>, std::vector<WireId>> wiresAtPoint;
    std::map<std::pair<int, int>, std::vector<PinAtPoint>> pinsAtPoint;

    for (const auto& [id, wire] : wires)
    {
        const auto& path = wire.getPath();
        if (path.size() < 2)
            continue;
        wiresAtPoint[{path.front().x, path.front().y}].push_back(id);
        wiresAtPoint[{path.back().x, path.back().y}].push_back(id);
    }

    for (const auto& anchor : pins)
        pinsAtPoint[{anchor.position.x, anchor.position.y}].push_back(
            {anchor.pin, anchor.type == PinType::OUTPUT}
        );

    WireUnion groups;
    for (const auto& [id, wire] : wires)
        groups.add(id);
    for (const auto& [coord, ids] : wiresAtPoint)
    {
        for (size_t i = 1; i < ids.size(); ++i)
            groups.unite(ids[0], ids[i]);
    }

    std::map<WireId, NetId> netOfRoot;
    for (auto& [id, wire] : wires)
    {
        if (wire.getPath().size() < 2)
            continue;

        const WireId root = groups.find(id);
        auto [it, inserted] = netOfRoot.emplace(root, result.nextNetId);
        if (inserted)
            result.nextNetId++;

        const NetId netId = it->second;
        result.nets[netId].addGeometry(id);
        result.wireNets[id] = netId;
    }

    for (const auto& [coord, pointPins] : pinsAtPoint)
    {
        auto wiresIt = wiresAtPoint.find(coord);
        if (wiresIt == wiresAtPoint.end() || wiresIt->second.empty())
            continue;

        auto netIt = netOfRoot.find(groups.find(wiresIt->second.front()));
        if (netIt == netOfRoot.end())
            continue;

        Net& net = result.nets[netIt->second];
        for (const PinAtPoint& pinAt : pointPins)
        {
            if (pinAt.isOutput)
            {
                if (!net.addDriver(pinAt.pin))
                {
                    std::cerr << "[Net Conflict] Short circuit: Component " << pinAt.pin.componentId
                              << " Pin " << pinAt.pin.pinIndex
                              << " connects to an already driven net!\n";
                }
            }
            else
            {
                net.addSink(pinAt.pin);
            }
            result.pinNets[{pinAt.pin.componentId, pinAt.pin.pinIndex, pinAt.isOutput}] =
                netIt->second;
        }
    }

    circuit.clearConnections();

    for (const auto& [netId, net] : result.nets)
    {
        // Shorted nets already have separate diagnostics and no unambiguous driver.
        if (!net.hasDriver())
            continue;

        const PinRef driver = *net.getDriver();
        for (const PinRef& sink : net.getSinks())
        {
            const ConnectionResult connectionResult = circuit.tryConnectComponents(
                driver.componentId, driver.pinIndex, sink.componentId, sink.pinIndex
            );
            if (connectionResult == ConnectionResult::OK)
                continue;

            result.rejectedConnections.push_back(
                {netId,
                 {driver.componentId, driver.pinIndex, sink.componentId, sink.pinIndex},
                 connectionResult}
            );
            const char* reason = "invalid connection";
            switch (connectionResult)
            {
            case ConnectionResult::INVALID_COMPONENT:
                reason = "missing component";
                break;
            case ConnectionResult::INVALID_PIN:
                reason = "invalid pin index";
                break;
            case ConnectionResult::INPUT_ALREADY_DRIVEN:
                reason = "input already driven";
                break;
            case ConnectionResult::OK:
                break;
            }
            if (result.status == SimulationResult::OK)
                result.status = SimulationResult::CONNECTION_REJECTED;

            std::cerr << "[Connection Rejected] Net " << netId << ": Component "
                      << driver.componentId << " Out[" << driver.pinIndex << "] -> Component "
                      << sink.componentId << " In[" << sink.pinIndex << "]: " << reason << ".\n";
        }
    }

    if (!result.rejectedConnections.empty())
    {
        circuit.clearConnections();
    }
    return result;
}
