#include "Geometry/GeometryQueries.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

HitResult hitGeometry(
    std::span<const ComponentGeometry> components,
    const std::map<WireId, Wire>& wires,
    float worldX,
    float worldY,
    GridCoords gridPos
)
{
    // 1. Pins — smallest, most specific targets, checked first
    for (const auto& component : components)
    {
        for (const auto& pin : component.pins)
            if (gridPos == pin.position)
                return {HitType::COMPONENT_PIN, component.componentId, pin.pin.pinIndex, pin.type};
    }

    // 2. Wire endpoints / bodies / junctions
    int endpointMatches = 0;
    WireId matchedWireId = INVALID_WIRE_ID;
    bool matchedIsStart = false;

    for (const auto& [id, wire] : wires)
    {
        const auto& path = wire.getPath();
        if (path.empty())
            continue;

        bool isStart = (gridPos == path.front());
        bool isEnd = (path.size() > 1 && gridPos == path.back());

        if (isStart || isEnd)
        {
            endpointMatches++;
            if (matchedWireId == INVALID_WIRE_ID)
            {
                matchedWireId = id;
                matchedIsStart = isStart;
            }
        }
    }

    if (endpointMatches >= 2)
    {
        return {HitType::WIRE_JUNCTION, -1, -1, PinType::INPUT, matchedWireId};
    }
    if (endpointMatches == 1)
    {
        return {
            matchedIsStart ? HitType::WIRE_START : HitType::WIRE_END,
            -1,
            -1,
            PinType::INPUT,
            matchedWireId
        };
    }

    for (const auto& [id, wire] : wires)
    {
        if (wire.containsPoint(gridPos))
        {
            return {HitType::WIRE_BODY, -1, -1, PinType::INPUT, id};
        }
    }

    for (const auto& component : components)
    {
        const float halfWidth = component.width * 0.5f;
        const float halfHeight = component.height * 0.5f;
        if (std::abs(worldX - component.centerX) <= halfWidth &&
            std::abs(worldY - component.centerY) <= halfHeight)
            return {HitType::COMPONENT_BODY, component.componentId};
    }

    return {};
}

bool overlapsComponent(std::span<const ComponentGeometry> components, int componentId)
{
    auto found = std::find_if(
        components.begin(),
        components.end(),
        [=](const auto& component) { return component.componentId == componentId; }
    );
    if (found == components.end())
        return false;
    const auto& moved = *found;
    for (const auto& other : components)
    {
        if (other.componentId == componentId)
            continue;
        // Tolerance admits exact grid-aligned boundary contact despite float rounding.
        constexpr float tolerance = 0.000001f;
        if (moved.origin == other.origin || (std::abs(moved.centerX - other.centerX) + tolerance <
                                                 (moved.width + other.width) * 0.5f &&
                                             std::abs(moved.centerY - other.centerY) + tolerance <
                                                 (moved.height + other.height) * 0.5f))
            return true;
        for (const auto& pin : moved.pins)
            for (const auto& otherPin : other.pins)
                if (pin.position == otherPin.position)
                    return true;
    }
    return false;
}

std::vector<WireJunction>
wireJunctions(const std::map<WireId, Wire>& wires, std::span<const PinAnchor> pins)
{
    struct PointData
    {
        int count = 0;
        PinState state = PinState::DISCONNECTED;
    };

    std::map<std::pair<int, int>, PointData> endpoints;
    std::set<std::pair<int, int>> pinPositions;
    for (const auto& pin : pins)
        pinPositions.emplace(pin.position.x, pin.position.y);
    auto record = [&](GridCoords point, PinState state)
    {
        auto& data = endpoints[{point.x, point.y}];
        ++data.count;
        if (state == PinState::ON ||
            (state == PinState::OFF && data.state == PinState::DISCONNECTED))
            data.state = state;
    };
    for (const auto& [id, wire] : wires)
    {
        const auto& path = wire.getPath();
        if (path.size() < 2)
            continue;
        record(path.front(), wire.getState());
        record(path.back(), wire.getState());
    }
    std::vector<WireJunction> result;
    for (const auto& [point, data] : endpoints)
        if (data.count >= 3 && !pinPositions.contains(point))
            result.push_back({{point.first, point.second}, data.state});
    return result;
}
