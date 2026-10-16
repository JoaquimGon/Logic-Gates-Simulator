#include "Geometry/WireRouting.h"

#include "Diagnostics/EditProfile.h"
#include "Geometry/GridMetrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <tuple>
#include <utility>

namespace
{
struct Obstacle
{
    int id;
    double left, right, bottom, top;
};


using Key = std::pair<int, int>;


Key key(GridCoords p)
{
    return {p.x, p.y};
}


GridCoords point(Key p)
{
    return {p.first, p.second};
}


std::int64_t distance(GridCoords a, GridCoords b)
{
    return std::abs(std::int64_t(a.x) - b.x) + std::abs(std::int64_t(a.y) - b.y);
}


/** @brief Build routing bounds from the visible body and pin anchors, adding half a grid cell of clearance. */
Obstacle componentObstacle(const ComponentGeometry& component)
{
    const double spacing = GridMetrics::Spacing;
    Obstacle bounds{
        component.componentId, (component.centerX - component.width / 2) / spacing,
        (component.centerX + component.width / 2) / spacing, (component.centerY - component.height / 2) / spacing,
        (component.centerY + component.height / 2) / spacing
    };
    for (const auto& pin : component.pins)
    {
        bounds.left = std::min(bounds.left, double(pin.position.x));
        bounds.right = std::max(bounds.right, double(pin.position.x));
        bounds.bottom = std::min(bounds.bottom, double(pin.position.y));
        bounds.top = std::max(bounds.top, double(pin.position.y));
    }
    constexpr double clearance = 0.5;
    bounds.left -= clearance;
    bounds.right += clearance;
    bounds.bottom -= clearance;
    bounds.top += clearance;
    return bounds;
}


bool crossesObstacle(GridCoords a, GridCoords b, const Obstacle& o)
{
    constexpr double tolerance = 1e-5;
    return a.y == b.y ? a.y > o.bottom + tolerance && a.y < o.top - tolerance &&
                            std::max(a.x, b.x) > o.left + tolerance && std::min(a.x, b.x) < o.right - tolerance
                      : a.x > o.left + tolerance && a.x < o.right - tolerance &&
                            std::max(a.y, b.y) > o.bottom + tolerance && std::min(a.y, b.y) < o.top - tolerance;
}


bool hasPin(const ComponentGeometry& component, GridCoords position)
{
    return std::any_of(
        component.pins.begin(), component.pins.end(), [&](const auto& pin) { return pin.position == position; }
    );
}


double bodyDirection(GridCoords pin, GridCoords next, const ComponentGeometry& component)
{
    return (double(next.x) - pin.x) * (component.centerX / GridMetrics::Spacing - pin.x) +
           (double(next.y) - pin.y) * (component.centerY / GridMetrics::Spacing - pin.y);
}


bool blockedSegment(const std::vector<GridCoords>& path, std::size_t i, const ComponentGeometry& component)
{
    // Outward pin access is allowed, just as it is for a new wire gesture.
    if ((i == 1 && hasPin(component, path.front()) && bodyDirection(path.front(), path[1], component) < 0) ||
        (i + 1 == path.size() && hasPin(component, path.back()) &&
         bodyDirection(path.back(), path[i - 1], component) < 0))
        return false;
    return crossesObstacle(path[i - 1], path[i], componentObstacle(component));
}
} // namespace


std::optional<std::vector<GridCoords>> routeWire(
    GridCoords start, GridCoords end, std::span<const ComponentGeometry> components,
    const std::map<WireId, Wire>& wires, NetId allowedNet, bool horizontalFirst
)
{
    edit_profile::Scope profileScope(edit_profile::Phase::Routing);
    if (start == end)
        return std::vector<GridCoords>{start};
    std::vector<Obstacle> obstacles;
    for (const auto& component : components)
        obstacles.push_back(componentObstacle(component));
    NetId targetNet = INVALID_NET_ID;
    for (const auto& [id, wire] : wires)
        if (wire.containsPoint(end))
        {
            targetNet = wire.getNet();
            break;
        }
    auto clear = [&](GridCoords a, GridCoords b, int access = -1)
    {
        for (const auto& o : obstacles)
        {
            if (o.id == access)
                continue;
            if (crossesObstacle(a, b, o))
                return false;
        }
        for (const auto& [id, wire] : wires)
        {
            if ((allowedNet != INVALID_NET_ID && wire.getNet() == allowedNet) ||
                (targetNet != INVALID_NET_ID && wire.getNet() == targetNet))
                continue;
            const auto& path = wire.getPath();
            for (std::size_t i = 1; i < path.size(); ++i)
            {
                const auto c = path[i - 1], d = path[i];
                int left = std::max(std::min(a.x, b.x), std::min(c.x, d.x));
                int right = std::min(std::max(a.x, b.x), std::max(c.x, d.x));
                int bottom = std::max(std::min(a.y, b.y), std::min(c.y, d.y));
                int top = std::min(std::max(a.y, b.y), std::max(c.y, d.y));
                if (left > right || bottom > top)
                    continue;
                if (left == right && bottom == top)
                {
                    const GridCoords crossing{left, bottom};
                    // Explicit endpoint joins are intentional. Interior crossings (including
                    // bends) do not form junctions, so they need no detour. Whole-wire endpoints
                    // remain obstacles to prevent an accidental join to another net.
                    if (crossing == start || crossing == end || (crossing != path.front() && crossing != path.back()))
                        continue;
                }
                return false;
            }
        }
        return true;
    };
    auto escape = [&](GridCoords pin) -> std::optional<GridCoords>
    {
        for (const auto& component : components)
            for (const auto& anchor : component.pins)
                if (anchor.position == pin)
                {
                    const auto o = *std::find_if(
                        obstacles.begin(), obstacles.end(),
                        [&](const auto& value) { return value.id == component.componentId; }
                    );
                    const std::array<double, 4> distances{
                        pin.x - o.left, o.right - pin.x, pin.y - o.bottom, o.top - pin.y
                    };
                    const auto side = std::min_element(distances.begin(), distances.end()) - distances.begin();
                    double x = pin.x, y = pin.y;
                    if (side == 0)
                        x = std::floor(o.left + 1e-5);
                    if (side == 1)
                        x = std::ceil(o.right - 1e-5);
                    if (side == 2)
                        y = std::floor(o.bottom + 1e-5);
                    if (side == 3)
                        y = std::ceil(o.top - 1e-5);
                    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
                        y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
                        return std::nullopt;
                    GridCoords outside{int(x), int(y)};
                    return clear(pin, outside, component.componentId) ? std::optional{outside} : std::nullopt;
                }
        return clear(pin, pin) ? std::optional{pin} : std::nullopt;
    };
    const auto from = escape(start), to = escape(end);
    if (!from || !to)
        return std::nullopt;
    auto finish = [&](std::vector<GridCoords> path)
    {
        path.insert(path.begin(), start);
        path.push_back(end);
        Wire wire;
        wire.setPath(path);
        wire.simplifyPath();
        return wire.getPath();
    };
    // Most routes need no search, including very distant endpoints.
    for (bool xFirst : {horizontalFirst, !horizontalFirst})
    {
        GridCoords bend = xFirst ? GridCoords{to->x, from->y} : GridCoords{from->x, to->y};
        if (clear(*from, bend) && clear(bend, *to))
            return finish({*from, bend, *to});
    }

    struct Visit
    {
        std::int64_t cost;
        Key previous;
    };

    std::map<Key, Visit> visited;
    using Entry = std::tuple<std::int64_t, std::int64_t, Key>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    visited.emplace(key(*from), Visit{0, key(*from)});
    queue.emplace(distance(*from, *to) * 10, 0, key(*from));
    const std::array<GridCoords, 4> directions = horizontalFirst
                                                     ? std::array<GridCoords, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}
                                                     : std::array<GridCoords, 4>{{{0, 1}, {0, -1}, {1, 0}, {-1, 0}}};
    constexpr std::size_t searchLimit = 20000;
    std::size_t expanded = 0;
    while (!queue.empty() && expanded++ < searchLimit && visited.size() < searchLimit)
    {
        auto [estimate, cost, current] = queue.top();
        queue.pop();
        if (visited.at(current).cost != cost)
            continue;
        if (current == key(*to))
        {
            std::vector<GridCoords> path;
            for (auto node = current;; node = visited.at(node).previous)
            {
                path.push_back(point(node));
                if (node == key(*from))
                    break;
            }
            std::reverse(path.begin(), path.end());
            return finish(std::move(path));
        }
        for (auto direction : directions)
        {
            const auto x = std::int64_t(current.first) + direction.x;
            const auto y = std::int64_t(current.second) + direction.y;
            if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
                y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
                continue;
            GridCoords next{int(x), int(y)};
            if (!clear(point(current), next))
                continue;
            const auto previous = visited.at(current).previous;
            const bool turns = previous != current && ((previous.first != current.first) != (next.x != current.first));
            const auto nextCost = cost + 10 + (turns ? 2 : 0);
            auto found = visited.find(key(next));
            if (found != visited.end() && found->second.cost <= nextCost)
                continue;
            visited.insert_or_assign(key(next), Visit{nextCost, current});
            queue.emplace(nextCost + distance(next, *to) * 10, nextCost, key(next));
        }
    }
    return std::nullopt;
}


bool rerouteMovedWires(
    std::span<const ComponentGeometry> before, std::span<const ComponentGeometry> after, std::span<const int> moved,
    std::map<WireId, Wire>& wires, std::span<const WireId> translatedWires
)
{
    std::vector<std::pair<GridCoords, GridCoords>> endpoints;
    for (const auto& old : before)
    {
        if (std::find(moved.begin(), moved.end(), old.componentId) == moved.end())
            continue;
        const auto current = std::find_if(
            after.begin(), after.end(), [&](const auto& value) { return value.componentId == old.componentId; }
        );
        if (current == after.end())
            continue;
        for (const auto& pin : old.pins)
            for (const auto& next : current->pins)
                if (pin.pin == next.pin && pin.type == next.type && pin.position != next.position)
                    endpoints.emplace_back(pin.position, next.position);
    }
    return rerouteWireEndpoints(endpoints, after, wires, translatedWires);
}


bool rerouteWireEndpoints(
    std::span<const std::pair<GridCoords, GridCoords>> moves, std::span<const ComponentGeometry> after,
    std::map<WireId, Wire>& wires, std::span<const WireId> translatedWires
)
{
    edit_profile::Scope profileScope(edit_profile::Phase::Routing);
    std::map<Key, GridCoords> endpoints;
    for (const auto& [from, to] : moves)
        if (from != to)
            endpoints.insert_or_assign(key(from), to);
    auto staged = wires;
    auto reserved = wires;
    // Old paths of wires which are moving are not obstacles at their former locations.
    for (const auto& [id, wire] : wires)
    {
        const auto& path = wire.getPath();
        if (std::find(translatedWires.begin(), translatedWires.end(), id) == translatedWires.end() &&
            path.size() >= 2 && (endpoints.contains(key(path.front())) || endpoints.contains(key(path.back()))))
            reserved.erase(id);
    }
    for (auto& [id, wire] : staged)
    {
        const auto path = wire.getPath();
        if (path.size() < 2 || std::find(translatedWires.begin(), translatedWires.end(), id) != translatedWires.end())
            continue;
        auto start = endpoints.find(key(path.front())), end = endpoints.find(key(path.back()));
        if (start == endpoints.end() && end == endpoints.end())
            continue;
        auto route = routeWire(
            start == endpoints.end() ? path.front() : start->second, end == endpoints.end() ? path.back() : end->second,
            after, reserved, wire.getNet()
        );
        if (!route)
            return false;
        wire.setPath(*route);
        reserved.insert_or_assign(id, wire);
    }
    wires = std::move(staged);
    return true;
}


bool fitWiresToComponents(
    std::span<const ComponentGeometry> components, std::span<const int> placed, std::map<WireId, Wire>& wires,
    WireId& nextWireId, std::span<const WireId> translatedWires
)
{
    edit_profile::Scope profileScope(edit_profile::Phase::Routing);
    auto staged = wires;
    auto nextId = nextWireId;
    auto translated = [&](WireId id)
    { return std::find(translatedWires.begin(), translatedWires.end(), id) != translatedWires.end(); };
    auto wasPlaced = [&](int id) { return std::find(placed.begin(), placed.end(), id) != placed.end(); };
    std::map<Key, int> degree;
    for (const auto& [id, wire] : staged)
        if (wire.getPath().size() >= 2)
        {
            ++degree[key(wire.getPath().front())];
            ++degree[key(wire.getPath().back())];
        }
    auto freeEnd = [&](GridCoords end)
    {
        return degree.contains(key(end)) && degree.at(key(end)) == 1 &&
               std::none_of(
                   components.begin(), components.end(), [&](const auto& component) { return hasPin(component, end); }
               );
    };
    auto bodyTail = [&](WireId id, const Wire& wire, GridCoords pin, const ComponentGeometry& component)
    {
        const auto& path = wire.getPath();
        const bool dangling =
            path.size() >= 2 &&
            ((path.front() == pin && freeEnd(path.back()) && bodyDirection(pin, path[1], component) > 0) ||
             (path.back() == pin && freeEnd(path.front()) && bodyDirection(pin, path[path.size() - 2], component) > 0));
        if (!dangling)
            return false;
        for (const auto& other : components)
            if (other.componentId != component.componentId)
                for (const auto& anchor : other.pins)
                    if (anchor.position != pin && wire.containsPoint(anchor.position))
                        return false;
        // Attached-wire rerouting can create a junction before normalization runs.
        for (const auto& [otherId, other] : wires)
            if (otherId != id && other.getPath().size() >= 2)
                for (const auto endpoint : {other.getPath().front(), other.getPath().back()})
                    if (endpoint != pin && wire.containsPoint(endpoint))
                        return false;
        return true;
    };
    // Classify tails before splitting at all newly landed pins. Otherwise another
    // pin on this body could falsely protect a previously dangling tail.
    for (auto it = staged.begin(); it != staged.end();)
    {
        bool remove = false;
        if (!translated(it->first))
            for (const auto& component : components)
                if (wasPlaced(component.componentId) && !remove)
                    for (const auto& pin : component.pins)
                    {
                        Wire first, second;
                        if (it->second.splitAt(pin.position, first, second))
                        {
                            if (bodyTail(it->first, first, pin.position, component))
                                it->second = std::move(second);
                            else if (bodyTail(it->first, second, pin.position, component))
                                it->second = std::move(first);
                        }
                        else if (bodyTail(it->first, it->second, pin.position, component))
                        {
                            remove = true;
                            break;
                        }
                    }
        if (remove)
            it = staged.erase(it);
        else
            ++it;
    }
    // Pins and whole-wire endpoints anchor connections, including new junctions
    // from attached-wire rerouting. Interior visual crossings remain separate.
    auto splitAt = [&](GridCoords position)
    {
        std::vector<std::pair<WireId, Wire>> fragments;
        for (auto& [id, wire] : staged)
        {
            if (translated(id))
                continue;
            Wire first, second;
            if (wire.splitAt(position, first, second))
            {
                wire = std::move(first);
                if (nextId == std::numeric_limits<WireId>::max())
                    return false;
                fragments.emplace_back(nextId++, std::move(second));
            }
        }
        for (auto& [id, fragment] : fragments)
            staged.emplace(id, std::move(fragment));
        return true;
    };
    for (const auto& component : components)
        for (const auto& pin : component.pins)
            if (!splitAt(pin.position))
                return false;
    for (const auto& [id, wire] : wires)
        if (wire.getPath().size() >= 2)
            if (!splitAt(wire.getPath().front()) || !splitAt(wire.getPath().back()))
                return false;


    // Only spans obstructed by the landed bodies need detours; translated group wires retain their shape.
    std::vector<WireId> affected;
    for (const auto& [id, wire] : staged)
    {
        if (translated(id))
            continue;
        const auto& path = wire.getPath();
        bool blocked = false;
        for (const auto& component : components)
            if (wasPlaced(component.componentId))
                for (std::size_t i = 1; i < path.size(); ++i)
                    blocked |= blockedSegment(path, i, component);
        if (blocked)
            affected.push_back(id);
    }

    // Reserve unaffected routes first so each replacement can avoid geometry already accepted.
    auto reserved = staged;
    for (const auto id : affected)
        reserved.erase(id);
    for (const auto id : affected)
    {
        auto& wire = staged.at(id);
        const auto& path = wire.getPath();
        std::vector<GridCoords> replacement;
        std::size_t first = 0;
        // Keep safe bends as anchors. Replacing a whole loop with its shortest
        // endpoint route could collapse it into another existing continuation.
        for (std::size_t last = 1; last < path.size(); ++last)
        {
            if (last + 1 < path.size() &&
                std::any_of(
                    components.begin(), components.end(),
                    [&](const auto& component)
                    {
                        return wasPlaced(component.componentId) &&
                               crossesObstacle(path[last], path[last], componentObstacle(component));
                    }
                ))
                continue;
            bool blocked = false;
            for (const auto& component : components)
                if (wasPlaced(component.componentId))
                    for (std::size_t i = first + 1; i <= last; ++i)
                        blocked |= blockedSegment(path, i, component);
            std::vector<GridCoords> span;
            if (blocked)
            {
                if (path[first] == path[last])
                    return false;
                const auto route = routeWire(path[first], path[last], components, reserved, wire.getNet());
                if (!route)
                    return false;
                span = *route;
            }
            else
                span.assign(path.begin() + first, path.begin() + last + 1);
            replacement.insert(replacement.end(), span.begin() + (replacement.empty() ? 0 : 1), span.end());
            first = last;
        }
        wire.setPath(replacement);
        wire.simplifyPath();
        reserved.insert_or_assign(id, wire);
    }
    wires = std::move(staged);
    nextWireId = nextId;
    return true;
}
