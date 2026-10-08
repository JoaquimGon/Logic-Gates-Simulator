#include "Geometry/WireRouting.h"

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
} // namespace

std::optional<std::vector<GridCoords>> routeWire(
    GridCoords start,
    GridCoords end,
    std::span<const ComponentGeometry> components,
    const std::map<WireId, Wire>& wires,
    NetId allowedNet,
    bool horizontalFirst
)
{
    if (start == end)
        return std::vector<GridCoords>{start};
    std::vector<Obstacle> obstacles;
    for (const auto& component : components)
    {
        const double spacing = GridMetrics::Spacing;
        Obstacle bounds{
            component.componentId,
            (component.centerX - component.width / 2) / spacing,
            (component.centerX + component.width / 2) / spacing,
            (component.centerY - component.height / 2) / spacing,
            (component.centerY + component.height / 2) / spacing
        };
        for (const auto& pin : component.pins)
        {
            bounds.left = std::min(bounds.left, double(pin.position.x));
            bounds.right = std::max(bounds.right, double(pin.position.x));
            bounds.bottom = std::min(bounds.bottom, double(pin.position.y));
            bounds.top = std::max(bounds.top, double(pin.position.y));
        }
        bounds.left -= 1;
        bounds.right += 1;
        bounds.bottom -= 1;
        bounds.top += 1;
        obstacles.push_back(bounds);
    }
    NetId targetNet = INVALID_NET_ID;
    for (const auto& [id, wire] : wires)
        if (wire.containsPoint(end))
        {
            targetNet = wire.getNet();
            break;
        }
    auto clear = [&](GridCoords a, GridCoords b, int access = -1)
    {
        constexpr double tolerance = 1e-5;
        for (const auto& o : obstacles)
        {
            if (o.id == access)
                continue;
            if (a.y == b.y ? a.y > o.bottom + tolerance && a.y < o.top - tolerance &&
                                 std::max(a.x, b.x) > o.left + tolerance &&
                                 std::min(a.x, b.x) < o.right - tolerance
                           : a.x > o.left + tolerance && a.x < o.right - tolerance &&
                                 std::max(a.y, b.y) > o.bottom + tolerance &&
                                 std::min(a.y, b.y) < o.top - tolerance)
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
                // Joining the intended endpoints is allowed; other routes remain separate.
                if (left == right && bottom == top &&
                    (GridCoords{left, bottom} == start || GridCoords{left, bottom} == end))
                    continue;
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
                        obstacles.begin(),
                        obstacles.end(),
                        [&](const auto& value) { return value.id == component.componentId; }
                    );
                    const std::array<double, 4> distances{
                        pin.x - o.left, o.right - pin.x, pin.y - o.bottom, o.top - pin.y
                    };
                    const auto side =
                        std::min_element(distances.begin(), distances.end()) - distances.begin();
                    double x = pin.x, y = pin.y;
                    if (side == 0)
                        x = std::floor(o.left + 1e-5);
                    if (side == 1)
                        x = std::ceil(o.right - 1e-5);
                    if (side == 2)
                        y = std::floor(o.bottom + 1e-5);
                    if (side == 3)
                        y = std::ceil(o.top - 1e-5);
                    if (x < std::numeric_limits<int>::min() ||
                        x > std::numeric_limits<int>::max() ||
                        y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
                        return std::nullopt;
                    GridCoords outside{int(x), int(y)};
                    return clear(pin, outside, component.componentId) ? std::optional{outside}
                                                                      : std::nullopt;
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
    const std::array<GridCoords, 4> directions =
        horizontalFirst ? std::array<GridCoords, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}
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
            const bool turns = previous != current &&
                               ((previous.first != current.first) != (next.x != current.first));
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
    std::span<const ComponentGeometry> before,
    std::span<const ComponentGeometry> after,
    std::span<const int> moved,
    std::map<WireId, Wire>& wires,
    std::span<const WireId> translatedWires
)
{
    std::map<Key, GridCoords> endpoints;
    for (const auto& old : before)
    {
        if (std::find(moved.begin(), moved.end(), old.componentId) == moved.end())
            continue;
        const auto current = std::find_if(
            after.begin(),
            after.end(),
            [&](const auto& value) { return value.componentId == old.componentId; }
        );
        if (current == after.end() || current->origin == old.origin)
            continue;
        for (const auto& pin : old.pins)
            for (const auto& next : current->pins)
                if (pin.pin == next.pin && pin.type == next.type && pin.position != next.position)
                    endpoints.insert_or_assign(key(pin.position), next.position);
    }
    auto staged = wires;
    auto reserved = wires;
    // Old paths of wires which are moving are not obstacles at their former locations.
    for (const auto& [id, wire] : wires)
    {
        const auto& path = wire.getPath();
        if (std::find(translatedWires.begin(), translatedWires.end(), id) ==
                translatedWires.end() &&
            path.size() >= 2 &&
            (endpoints.contains(key(path.front())) || endpoints.contains(key(path.back()))))
            reserved.erase(id);
    }
    for (auto& [id, wire] : staged)
    {
        const auto path = wire.getPath();
        if (path.size() < 2 ||
            std::find(translatedWires.begin(), translatedWires.end(), id) != translatedWires.end())
            continue;
        auto start = endpoints.find(key(path.front())), end = endpoints.find(key(path.back()));
        if (start == endpoints.end() && end == endpoints.end())
            continue;
        auto route = routeWire(
            start == endpoints.end() ? path.front() : start->second,
            end == endpoints.end() ? path.back() : end->second,
            after,
            reserved,
            wire.getNet()
        );
        if (!route)
            return false;
        wire.setPath(*route);
        reserved.insert_or_assign(id, wire);
    }
    wires = std::move(staged);
    return true;
}
