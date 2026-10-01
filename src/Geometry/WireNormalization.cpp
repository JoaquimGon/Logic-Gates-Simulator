#include "Geometry/WireNormalization.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace
{
class WireNormalizer
{
  public:
    WireNormalizer(
        std::map<WireId, Wire>& wires, std::span<const PinAnchor> pins, WireId& nextWireId
    )
        : m_wires(wires), m_pins(pins), m_nextWireId(nextWireId)
    {
    }

    void run();

  private:
    std::map<WireId, Wire>& m_wires;
    std::span<const PinAnchor> m_pins;
    WireId& m_nextWireId;
    WireId insertWire(Wire wire);
    Wire* editWire(WireId id);
    std::vector<WireId> getWireIds() const;
    std::pair<WireId, WireId> insertWires(Wire a, Wire b);
    bool splitWireGeometry(WireId id, GridCoords point, Wire& outA, Wire& outB);
    void dropDegenerateWires();
    void mergeOverlaps();
    void splitJunctions();
    void healJunctions();
};

WireId WireNormalizer::insertWire(Wire wire)
{
    WireId id = m_nextWireId++;
    m_wires.emplace(id, std::move(wire));
    return id;
}

Wire* WireNormalizer::editWire(WireId id)
{
    auto it = m_wires.find(id);
    return it != m_wires.end() ? &it->second : nullptr;
}

std::vector<WireId> WireNormalizer::getWireIds() const
{
    std::vector<WireId> ids;
    ids.reserve(m_wires.size());
    for (const auto& [id, wire] : m_wires)
        ids.push_back(id);
    return ids;
}

bool WireNormalizer::splitWireGeometry(WireId id, GridCoords point, Wire& outA, Wire& outB)
{
    Wire* wire = editWire(id);
    if (!wire)
        return false;
    if (!wire->splitAt(point, outA, outB))
        return false;

    m_wires.erase(id);
    return true;
}

std::pair<WireId, WireId> WireNormalizer::insertWires(Wire a, Wire b)
{
    return {insertWire(std::move(a)), insertWire(std::move(b))};
}

void WireNormalizer::run()
{
    dropDegenerateWires();

    mergeOverlaps();
    splitJunctions();
    dropDegenerateWires();
    healJunctions();
}

void WireNormalizer::mergeOverlaps()
{
    bool overlapMerged = true;
    while (overlapMerged)
    {
        overlapMerged = false;
        auto wireIds = getWireIds();

        for (size_t i = 0; i < wireIds.size() && !overlapMerged; ++i)
        {
            for (size_t j = i + 1; j < wireIds.size() && !overlapMerged; ++j)
            {
                Wire* w1 = editWire(wireIds[i]);
                Wire* w2 = editWire(wireIds[j]);
                if (!w1 || !w2)
                    continue;

                const auto& p1 = w1->getPath();
                const auto& p2 = w2->getPath();
                if (p1.size() < 2 || p2.size() < 2)
                    continue;

                GridCoords oStart, oEnd;
                // A merge erases both wires; check the flag before reading either path.
                for (size_t s1 = 0; !overlapMerged && s1 + 1 < p1.size(); ++s1)
                {
                    for (size_t s2 = 0; !overlapMerged && s2 + 1 < p2.size(); ++s2)
                    {
                        if (collinearOverlap(p1[s1], p1[s1 + 1], p2[s2], p2[s2 + 1], oStart, oEnd))
                        {
                            // Copy before erasing either route and invalidating their paths.
                            Wire wire1 = *w1;
                            Wire wire2 = *w2;
                            m_wires.erase(wireIds[i]);
                            m_wires.erase(wireIds[j]);

                            // Force both wires to have vertices at the overlap
                            // bounds
                            std::vector<Wire> fragments;

                            auto splitAndCollect = [&](Wire w)
                            {
                                Wire a, rem;
                                if (w.splitAt(oStart, a, rem))
                                {
                                    fragments.push_back(a);
                                    Wire b, c;
                                    if (rem.splitAt(oEnd, b, c))
                                    {
                                        fragments.push_back(b);
                                        fragments.push_back(c);
                                    }
                                    else
                                    {
                                        fragments.push_back(rem);
                                    }
                                }
                                else
                                {
                                    Wire b, c;
                                    if (w.splitAt(oEnd, b, c))
                                    {
                                        fragments.push_back(b);
                                        fragments.push_back(c);
                                    }
                                    else
                                    {
                                        fragments.push_back(w);
                                    }
                                }
                            };

                            splitAndCollect(wire1);
                            splitAndCollect(wire2);

                            // Endpoints alone cannot distinguish different bent routes.
                            for (auto& frag : fragments)
                            {
                                frag.simplifyPath();
                                if (frag.getPath().size() < 2)
                                    continue;

                                bool duplicate = false;
                                for (const auto& [id, existing] : m_wires)
                                {
                                    const auto& ep = existing.getPath();
                                    const auto& fp = frag.getPath();
                                    if (ep == fp ||
                                        std::equal(ep.begin(), ep.end(), fp.rbegin(), fp.rend()))
                                    {
                                        duplicate = true;
                                        break;
                                    }
                                }
                                if (!duplicate)
                                {
                                    insertWire(std::move(frag));
                                }
                            }

                            overlapMerged = true;
                        }
                    }
                }
            }
        }
    }
}

void WireNormalizer::splitJunctions()
{
    std::vector<GridCoords> junctions;
    for (const auto& pin : m_pins)
        junctions.push_back(pin.position);
    for (const auto& [id, wire] : m_wires)
    {
        if (wire.getPath().size() < 2)
            continue;
        junctions.push_back(wire.getPath().front());
        junctions.push_back(wire.getPath().back());
    }

    for (const GridCoords& point : junctions)
    {
        for (WireId id : getWireIds())
        {
            Wire* wire = editWire(id);
            if (!wire || wire->getPath().size() < 2)
                continue;
            if (wire->getPath().front() == point || wire->getPath().back() == point)
                continue;
            if (!wire->containsPoint(point))
                continue;

            Wire wireA, wireB;
            if (splitWireGeometry(id, point, wireA, wireB))
            {
                insertWires(std::move(wireA), std::move(wireB));
            }
        }
    }
}

void WireNormalizer::healJunctions()
{
    bool healed = true;
    while (healed)
    {
        healed = false;

        std::map<std::pair<int, int>, bool> hasPin;
        for (const auto& pin : m_pins)
            hasPin[{pin.position.x, pin.position.y}] = true;

        struct EndpointEntry
        {
            WireId wireId;
            bool isStart;
        };

        std::map<std::pair<int, int>, std::vector<EndpointEntry>> junctionMap;

        for (const auto& [id, wire] : m_wires)
        {
            const auto& path = wire.getPath();
            if (path.size() >= 2)
            {
                junctionMap[{path.front().x, path.front().y}].push_back({id, true});
                junctionMap[{path.back().x, path.back().y}].push_back({id, false});
            }
        }

        for (const auto& [coord, entries] : junctionMap)
        {
            if (hasPin[coord] || entries.size() != 2)
                continue;

            WireId id1 = entries[0].wireId;
            WireId id2 = entries[1].wireId;
            if (id1 == id2)
                continue; // Loop onto itself

            Wire* w1 = editWire(id1);
            Wire* w2 = editWire(id2);
            if (!w1 || !w2)
                continue;

            bool id1Start = entries[0].isStart;
            bool id2Start = entries[1].isStart;

            std::vector<GridCoords> p1 = w1->getPath();
            std::vector<GridCoords> p2 = w2->getPath();

            // Orient w1 so it ends at the junction coord
            if (id1Start)
                std::reverse(p1.begin(), p1.end());
            // Orient w2 so it starts at the junction coord
            if (!id2Start)
                std::reverse(p2.begin(), p2.end());

            // Stitch p2 onto p1 (dropping duplicate junction point)
            p1.insert(p1.end(), p2.begin() + 1, p2.end());

            Wire mergedWire;
            mergedWire.setPath(p1);
            mergedWire.simplifyPath(); // Drops the seam if collinear!

            m_wires.erase(id1);
            m_wires.erase(id2);
            insertWire(std::move(mergedWire));

            healed = true;
            break;
        }
    }
}

void WireNormalizer::dropDegenerateWires()
{
    // Consecutive duplicate nodes can otherwise invent drawable/electrical endpoints.
    for (auto& [id, wire] : m_wires)
        wire.simplifyPath();
    std::erase_if(
        m_wires,
        [](const std::pair<const WireId, Wire>& entry) { return entry.second.getPath().size() < 2; }
    );
}

} // namespace

WireChanges
normalizeWires(std::map<WireId, Wire>& wires, std::span<const PinAnchor> pins, WireId& nextWireId)
{
    std::vector<WireId> before;
    for (const auto& [id, wire] : wires)
        before.push_back(id);
    WireNormalizer(wires, pins, nextWireId).run();
    WireChanges changes;
    for (WireId id : before)
        if (!wires.contains(id))
            changes.removed.push_back(id);
    for (const auto& [id, wire] : wires)
        if (!std::binary_search(before.begin(), before.end(), id))
            changes.added.push_back(id);
    return changes;
}

bool collinearOverlap(
    GridCoords a, GridCoords b, GridCoords c, GridCoords d, GridCoords& outStart, GridCoords& outEnd
)
{
    bool abHorizontal = (a.y == b.y), abVertical = (a.x == b.x);
    bool cdHorizontal = (c.y == d.y), cdVertical = (c.x == d.x);

    if (abHorizontal && cdHorizontal && a.y == c.y)
    {
        int aMin = std::min(a.x, b.x), aMax = std::max(a.x, b.x);
        int cMin = std::min(c.x, d.x), cMax = std::max(c.x, d.x);
        int oMin = std::max(aMin, cMin), oMax = std::min(aMax, cMax);
        if (oMax > oMin)
        {
            if (std::abs(a.x - oMin) < std::abs(a.x - oMax))
            {
                outStart = {oMin, a.y};
                outEnd = {oMax, a.y};
            }
            else
            {
                outStart = {oMax, a.y};
                outEnd = {oMin, a.y};
            }
            return true;
        }
    }
    else if (abVertical && cdVertical && a.x == c.x)
    {
        int aMin = std::min(a.y, b.y), aMax = std::max(a.y, b.y);
        int cMin = std::min(c.y, d.y), cMax = std::max(c.y, d.y);
        int oMin = std::max(aMin, cMin), oMax = std::min(aMax, cMax);
        if (oMax > oMin)
        {
            if (std::abs(a.y - oMin) < std::abs(a.y - oMax))
            {
                outStart = {a.x, oMin};
                outEnd = {a.x, oMax};
            }
            else
            {
                outStart = {a.x, oMax};
                outEnd = {a.x, oMin};
            }
            return true;
        }
    }
    return false;
}
