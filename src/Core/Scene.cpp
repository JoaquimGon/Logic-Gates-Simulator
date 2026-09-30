#include "Scene.h"

namespace
{
/*
 * @brief Union-find over wire ids.
 * Two wires that share an endpoint coordinate are the same net, transitively.
 * That is the whole of the connectivity rule, so one pass over the endpoint map
 * settles every net in the scene - no fixpoint, and nothing to re-derive per
 * frame.
 */
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

int Scene::addGate(
    GateType type,
    GridCoords gridPos,
    glm::vec2 size,
    const std::string& shaderName,
    std::vector<PinUI> inputs,
    std::vector<PinUI> outputs
)
{
    int id = m_circuit.addGate(type);
    m_componentViews.emplace(
        id,
        std::make_unique<GateView>(
            gridPos, id, size, shaderName, std::move(inputs), std::move(outputs)
        )
    );

    // The new pins may have landed on geometry that is already drawn, so the
    // nets are re-derived rather than assumed.
    rebuildNets();
    return id;
}


int Scene::addInputPin(
    GridCoords gridPos, glm::vec2 size, const std::string& shaderName, bool initialState
)
{
    int id = m_circuit.addInputPin(initialState);
    m_componentViews.emplace(id, std::make_unique<InputPinView>(gridPos, id, size, shaderName));

    rebuildNets();
    return id;
}


void Scene::removeComponent(int componentId)
{
    m_circuit.delComponent(componentId);
    m_componentViews.erase(componentId);

    // The wires are left exactly where they were drawn: the rebuild below
    // simply stops attaching pins whose component no longer exists, so nothing
    // has to be walked here to detach them by hand.
    rebuildNets();
}


ComponentView* Scene::getComponentView(int componentId)
{
    auto it = m_componentViews.find(componentId);
    return it != m_componentViews.end() ? it->second.get() : nullptr;
}


Component* Scene::getLogicComponent(int componentId)
{
    return m_circuit.getComponent(componentId);
}


WireId Scene::insertWire(Wire wire)
{
    // Ids are handed out in order and never recycled: that is what makes a
    // cached WireId keep referring to the same wire for the rest of the
    // session.
    WireId id = m_nextWireId++;
    m_wires.emplace(id, std::move(wire));
    return id;
}


Wire* Scene::getWire(WireId id)
{
    auto it = m_wires.find(id);
    return it != m_wires.end() ? &it->second : nullptr;
}


const Wire* Scene::getWire(WireId id) const
{
    auto it = m_wires.find(id);
    return it != m_wires.end() ? &it->second : nullptr;
}


std::vector<WireId> Scene::getWireIds() const
{
    std::vector<WireId> ids;
    ids.reserve(m_wires.size());
    for (const auto& [id, wire] : m_wires)
        ids.push_back(id);
    return ids;
}


std::optional<WireId> Scene::commitWire(Wire wire)
{
    wire.simplifyPath();
    if (wire.getPath().size() < 2)
    {
        // A one-point wire is neither drawable nor electrical, so it is never
        // stored.
        return std::nullopt;
    }

    // No orientation handling is needed here any more. Direction used to be
    // encoded in front()/back() because Wire carried the driver and the sinks
    // itself, which meant a wire drawn sink-to-source had to be flipped before
    // it could be split correctly. A wire is now geometry plus a net, so either
    // direction is equally valid.
    WireId id = insertWire(std::move(wire));

    // The new geometry may join pins, wires, or nothing at all; the nets are
    // re-derived from it either way, and they are what feeds the Circuit.
    rebuildNets();
    return id;
}


std::optional<Wire> Scene::extractWire(WireId id)
{
    auto it = m_wires.find(id);
    if (it == m_wires.end())
        return std::nullopt;

    Wire wire = std::move(it->second);
    m_wires.erase(it);

    rebuildNets();
    return wire;
}


bool Scene::splitWireGeometry(WireId id, GridCoords point, Wire& outA, Wire& outB)
{
    Wire* wire = getWire(id);
    if (!wire)
        return false;
    if (!wire->splitAt(point, outA, outB))
        return false;

    m_wires.erase(id);
    return true;
}


bool Scene::splitWireAt(WireId id, GridCoords point, Wire& outA, Wire& outB)
{
    if (!splitWireGeometry(id, point, outA, outB))
        return false;

    rebuildNets();
    return true;
}


std::pair<WireId, WireId> Scene::insertWires(Wire a, Wire b)
{
    return {insertWire(std::move(a)), insertWire(std::move(b))};
}


std::pair<WireId, WireId> Scene::addWires(Wire a, Wire b)
{
    std::pair<WireId, WireId> ids = insertWires(std::move(a), std::move(b));

    rebuildNets();
    return ids;
}


bool Scene::removeWire(WireId id)
{
    const bool removed = m_wires.erase(id) > 0;
    if (removed)
        rebuildNets();
    return removed;
}


void Scene::rebuildNets()
{
    // Connectivity is a property of the geometry, but it is *derived* here and
    // only on an edit - never while rendering. settleGeometry() makes junctions
    // real path nodes first, after which the nets are a plain
    // connected-component walk instead of the geometric fixpoint (healWires)
    // plus per-frame BFS (syncVisuals) that this replaces.
    settleGeometry();
    collectNets();
    emitNetEdges();
}


namespace
{
// Returns true if segment [p1, p2] contains point p strictly between its
// endpoints
bool isStrictlyBetween(GridCoords p, GridCoords a, GridCoords b)
{
    if (p == a || p == b)
        return false;
    int cross = (p.y - a.y) * (b.x - a.x) - (p.x - a.x) * (b.y - a.y);
    if (cross != 0)
        return false;
    return (
        p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
        p.y <= std::max(a.y, b.y)
    );
}
} // namespace

void Scene::settleGeometry()
{
    dropDegenerateWires();

    // -------------------------------------------------------------------------
    // STEP 1: Merge collinear overlapping wire segments into a single unified
    // wire. This prevents duplicate wires when drawing over an existing line.
    // -------------------------------------------------------------------------
    bool overlapMerged = true;
    while (overlapMerged)
    {
        overlapMerged = false;
        auto wireIds = getWireIds();

        for (size_t i = 0; i < wireIds.size() && !overlapMerged; ++i)
        {
            for (size_t j = i + 1; j < wireIds.size() && !overlapMerged; ++j)
            {
                Wire* w1 = getWire(wireIds[i]);
                Wire* w2 = getWire(wireIds[j]);
                if (!w1 || !w2)
                    continue;

                const auto& p1 = w1->getPath();
                const auto& p2 = w2->getPath();
                if (p1.size() < 2 || p2.size() < 2)
                    continue;

                GridCoords oStart, oEnd;
                for (size_t s1 = 0; s1 + 1 < p1.size() && !overlapMerged; ++s1)
                {
                    for (size_t s2 = 0; s2 + 1 < p2.size() && !overlapMerged; ++s2)
                    {
                        if (getCollinearOverlap(
                                p1[s1], p1[s1 + 1], p2[s2], p2[s2 + 1], oStart, oEnd
                            ))
                        {
                            // Extract both wires, union their points
                            Wire wire1 = *w1;
                            Wire wire2 = *w2;
                            m_wires.erase(wireIds[i]);
                            m_wires.erase(wireIds[j]);

                            // Force both wires to have vertices at the overlap
                            // bounds
                            Wire w1A, w1B, w2A, w2B;
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

                            // Re-insert all non-duplicate fragments
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
                                    if ((ep.front() == fp.front() && ep.back() == fp.back()) ||
                                        (ep.front() == fp.back() && ep.back() == fp.front()))
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

    // -------------------------------------------------------------------------
    // STEP 2: Split wires at all genuine branch points (pins and other wire
    // endpoints)
    // -------------------------------------------------------------------------
    std::vector<GridCoords> junctions;
    for (const auto& [id, component] : m_componentViews)
    {
        for (const auto& pin : component->getInputPins())
            junctions.push_back(component->getAbsolutePinGridPos(pin));
        for (const auto& pin : component->getOutputPins())
            junctions.push_back(component->getAbsolutePinGridPos(pin));
    }
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
            Wire* wire = getWire(id);
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

    dropDegenerateWires();

    // -------------------------------------------------------------------------
    // STEP 3: HEAL DEGREE-2 JUNCTIONS
    // If exactly 2 wire endpoints meet at a coordinate with NO pin, merge them
    // into a single wire and call simplifyPath() to eliminate collinear seams.
    // -------------------------------------------------------------------------
    bool healed = true;
    while (healed)
    {
        healed = false;

        std::map<std::pair<int, int>, bool> hasPin;
        for (const auto& [id, component] : m_componentViews)
        {
            for (const auto& pin : component->getInputPins())
            {
                auto p = component->getAbsolutePinGridPos(pin);
                hasPin[{p.x, p.y}] = true;
            }
            for (const auto& pin : component->getOutputPins())
            {
                auto p = component->getAbsolutePinGridPos(pin);
                hasPin[{p.x, p.y}] = true;
            }
        }

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

            Wire* w1 = getWire(id1);
            Wire* w2 = getWire(id2);
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


void Scene::dropDegenerateWires()
{
    // A one-point fragment is neither drawable nor electrical, yet it used to
    // be counted as a topology endpoint and could invent junctions that were
    // not there.
    std::erase_if(
        m_wires,
        [](const std::pair<const WireId, Wire>& entry) { return entry.second.getPath().size() < 2; }
    );
}


void Scene::collectNets()
{
    m_nets.clear();
    m_pinNet.clear();

    struct PinAtPoint
    {
        PinRef pin;
        bool isOutput;
    };

    std::map<std::pair<int, int>, std::vector<WireId>> wiresAtPoint;
    std::map<std::pair<int, int>, std::vector<PinAtPoint>> pinsAtPoint;

    for (const auto& [id, wire] : m_wires)
    {
        const auto& path = wire.getPath();
        if (path.size() < 2)
            continue;
        wiresAtPoint[{path.front().x, path.front().y}].push_back(id);
        wiresAtPoint[{path.back().x, path.back().y}].push_back(id);
    }

    for (const auto& [id, component] : m_componentViews)
    {
        for (const auto& pin : component->getInputPins())
        {
            GridCoords pos = component->getAbsolutePinGridPos(pin);
            pinsAtPoint[{pos.x, pos.y}].push_back({{id, static_cast<int>(pin.pin_index)}, false});
        }
        for (const auto& pin : component->getOutputPins())
        {
            GridCoords pos = component->getAbsolutePinGridPos(pin);
            pinsAtPoint[{pos.x, pos.y}].push_back({{id, static_cast<int>(pin.pin_index)}, true});
        }
    }

    WireUnion wires;
    for (const auto& [id, wire] : m_wires)
        wires.add(id);
    for (const auto& [coord, ids] : wiresAtPoint)
    {
        for (size_t i = 1; i < ids.size(); ++i)
            wires.unite(ids[0], ids[i]);
    }

    std::map<WireId, NetId> netOfRoot;
    for (auto& [id, wire] : m_wires)
    {
        if (wire.getPath().size() < 2)
            continue;

        const WireId root = wires.find(id);
        auto [it, inserted] = netOfRoot.emplace(root, m_nextNetId);
        if (inserted)
            m_nextNetId++;

        const NetId netId = it->second;
        m_nets[netId].addGeometry(id);
        wire.setNet(netId);
    }

    for (const auto& [coord, pins] : pinsAtPoint)
    {
        auto wiresIt = wiresAtPoint.find(coord);
        if (wiresIt == wiresAtPoint.end() || wiresIt->second.empty())
            continue;

        auto netIt = netOfRoot.find(wires.find(wiresIt->second.front()));
        if (netIt == netOfRoot.end())
            continue;

        Net& net = m_nets[netIt->second];
        for (const PinAtPoint& pinAt : pins)
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
            m_pinNet[{pinAt.pin.componentId, pinAt.pin.pinIndex, pinAt.isOutput}] = netIt->second;
        }
    }
}


void Scene::emitNetEdges()
{
    m_circuit.clearConnections();

    for (const auto& [netId, net] : m_nets)
    {
        // If the net is shorted, do not propagate ambiguous logic values
        if (!net.hasDriver())
            continue;

        const PinRef driver = *net.getDriver();
        for (const PinRef& sink : net.getSinks())
        {
            if (sink.componentId == driver.componentId)
                continue;
            m_circuit.connectComponents(driver.componentId, sink.componentId, sink.pinIndex);
        }
    }
}


const Net* Scene::getNet(NetId id) const
{
    auto it = m_nets.find(id);
    return it != m_nets.end() ? &it->second : nullptr;
}


NetId Scene::netOfWire(WireId id) const
{
    const Wire* wire = getWire(id);
    return wire ? wire->getNet() : INVALID_NET_ID;
}


NetId Scene::netOfPin(const PinRef& pin, PinType type) const
{
    auto it = m_pinNet.find({pin.componentId, pin.pinIndex, type == PinType::OUTPUT});
    return it != m_pinNet.end() ? it->second : INVALID_NET_ID;
}


PinState Scene::pinState(const PinRef& pin, PinType type)
{
    if (!pin.isConnected())
        return PinState::DISCONNECTED;

    if (type == PinType::OUTPUT)
    {
        if (Component* driver = m_circuit.getComponent(pin.componentId))
        {
            // Only show ON (green) if the driver is actively HIGH; otherwise
            // keep it neutral (blue)
            return driver->getStateOutPin(pin.pinIndex) ? PinState::ON : PinState::DISCONNECTED;
        }
        return PinState::DISCONNECTED;
    }

    // A sink or dangling branch remains disconnected while drawing
    const Net* net = getNet(netOfPin(pin, type));
    if (!net || !net->hasDriver())
        return PinState::DISCONNECTED;

    return net->getState() == PinState::ON ? PinState::ON : PinState::DISCONNECTED;
}


HitResult Scene::hitTest(glm::vec2 worldPos, GridCoords gridPos) const
{
    // 1. Pins — smallest, most specific targets, checked first
    for (const auto& [id, component] : m_componentViews)
    {
        for (const auto& pin : component->getInputPins())
            if (gridPos == component->getAbsolutePinGridPos(pin))
                return {
                    HitType::COMPONENT_PIN, id, static_cast<int>(pin.pin_index), PinType::INPUT, -1
                };

        for (const auto& pin : component->getOutputPins())
            if (gridPos == component->getAbsolutePinGridPos(pin))
                return {
                    HitType::COMPONENT_PIN, id, static_cast<int>(pin.pin_index), PinType::OUTPUT, -1
                };
    }

    // 2. Wire endpoints / bodies / junctions
    int endpointMatches = 0;
    WireId matchedWireId = INVALID_WIRE_ID;
    bool matchedIsStart = false;

    for (const auto& [id, wire] : m_wires)
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

    for (const auto& [id, wire] : m_wires)
    {
        if (wire.containsPoint(gridPos))
        {
            return {HitType::WIRE_BODY, -1, -1, PinType::INPUT, id};
        }
    }

    for (const auto& [id, component] : m_componentViews)
    {
        glm::vec2 halfSize = (component->getSize() * 0.5f) - glm::vec2(0.015f);

        glm::vec2 delta = worldPos - component->getPosition();
        if (std::abs(delta.x) <= halfSize.x && std::abs(delta.y) <= halfSize.y)
            return {HitType::COMPONENT_BODY, id, -1, PinType::INPUT, -1};
    }

    return {};
}


bool Scene::getCollinearOverlap(
    GridCoords a, GridCoords b, GridCoords c, GridCoords d, GridCoords& outStart, GridCoords& outEnd
) const
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


bool Scene::updateClocks(float deltaTime)
{
    return m_circuit.updateClocks(deltaTime);
}


EvalOrderResult Scene::propagate()
{
    return m_circuit.propagate();
}


int Scene::addClock(
    GridCoords gridPos, glm::vec2 size, const std::string& shaderName, float frequencyHz
)
{
    int id = m_circuit.addClock(frequencyHz);
    m_componentViews.emplace(id, std::make_unique<ClockView>(gridPos, id, size, shaderName));

    rebuildNets();
    return id;
}


void Scene::togglePauseAllClocks()
{
    for (auto& [id, view] : m_componentViews)
    {
        if (auto* clk = dynamic_cast<Clock*>(m_circuit.getComponent(id)))
        {
            clk->togglePause();
        }
    }
}


void Scene::stepAllClocks()
{
    bool anyStepped = false;
    for (auto& [id, view] : m_componentViews)
    {
        if (auto* clk = dynamic_cast<Clock*>(m_circuit.getComponent(id)))
        {
            anyStepped |= clk->step();
        }
    }
    if (anyStepped)
    {
        markSimulationDirty();
    }
}


void Scene::setAllClocksFrequency(float hz)
{
    for (auto& [id, view] : m_componentViews)
    {
        if (auto* clk = dynamic_cast<Clock*>(m_circuit.getComponent(id)))
        {
            clk->setFrequency(hz);
        }
    }
}


void Scene::syncVisuals()
{
    for (auto& [id, view] : m_componentViews)
    {
        Component* comp = m_circuit.getComponent(id);
        if (!comp)
            continue;

        auto& outputs = view->getOutputPins();
        for (size_t i = 0; i < outputs.size(); ++i)
            outputs[i].state =
                comp->getStateOutPin(static_cast<int>(i)) ? PinState::ON : PinState::OFF;

        auto inSignals = comp->getStateInPins();
        auto& inputs = view->getInputPins();
        for (size_t i = 0; i < inSignals.size() && i < inputs.size(); ++i)
            inputs[i].state = inSignals[i] ? PinState::ON : PinState::OFF;
    }

    // Refresh Net states from single driver
    for (auto& [netId, net] : m_nets)
    {
        if (net.shorted())
        {
            net.setState(PinState::DISCONNECTED);
        }
        else if (net.hasDriver())
        {
            Component* driver = m_circuit.getComponent(net.getDriver()->componentId);
            net.setState(
                driver ? (driver->getStateOutPin(net.getDriver()->pinIndex) ? PinState::ON
                                                                            : PinState::OFF)
                       : PinState::DISCONNECTED
            );
        }
        else
        {
            net.setState(PinState::DISCONNECTED);
        }
    }

    // Mirror to wires in O(wires)
    for (auto& [id, wire] : m_wires)
    {
        const Net* net = getNet(wire.getNet());
        wire.setState(net ? net->getState() : PinState::DISCONNECTED);
    }
}


bool Scene::handleClick(int componentId)
{
    ComponentView* view = getComponentView(componentId);
    if (view)
    {
        bool consumed = view->onClick(m_circuit);
        if (consumed)
        {
            m_circuit.markStateDirty();
        }
        return consumed;
    }
    return false;
}


std::vector<glm::vec3> Scene::getWireIntersections() const
{
    std::vector<glm::vec3> intersections;

    struct PointData
    {
        int count = 0;
        float stateVal = 0.0f;
    };

    std::map<std::pair<int, int>, PointData> endpointMap;
    std::map<std::pair<int, int>, bool> componentPinPositions;

    for (const auto& [id, component] : m_componentViews)
    {
        for (const auto& pin : component->getInputPins())
        {
            GridCoords pos = component->getAbsolutePinGridPos(pin);
            componentPinPositions[{pos.x, pos.y}] = true;
        }
        for (const auto& pin : component->getOutputPins())
        {
            GridCoords pos = component->getAbsolutePinGridPos(pin);
            componentPinPositions[{pos.x, pos.y}] = true;
        }
    }

    for (const auto& [id, wire] : m_wires)
    {
        const auto& path = wire.getPath();
        if (path.size() < 2)
            continue;

        float stateVal = 0.0f; // DISCONNECTED
        if (wire.getState() == PinState::ON)
            stateVal = 1.0f;
        else if (wire.getState() == PinState::OFF)
            stateVal = 2.0f;

        // Register START endpoint
        auto startCoord = std::make_pair(path.front().x, path.front().y);
        endpointMap[startCoord].count++;
        if (stateVal == 1.0f || (stateVal == 2.0f && endpointMap[startCoord].stateVal == 0.0f))
            endpointMap[startCoord].stateVal = stateVal; // Priority: ON > OFF > DISCONNECTED

        // Register END endpoint
        if (path.size() > 1)
        {
            auto endCoord = std::make_pair(path.back().x, path.back().y);
            endpointMap[endCoord].count++;
            if (stateVal == 1.0f || (stateVal == 2.0f && endpointMap[endCoord].stateVal == 0.0f))
                endpointMap[endCoord].stateVal = stateVal;
        }
    }

    // Dot only appears if 3 or more topological endpoints meet here!
    for (const auto& [coord, data] : endpointMap)
    {
        // A component pin already has its own visual marker. It is a terminal,
        // not a free-standing wire junction, even when several wire fragments
        // share its coordinates.
        if (data.count >= 3 && componentPinPositions.find(coord) == componentPinPositions.end())
        {
            intersections.push_back(
                {static_cast<float>(coord.first), static_cast<float>(coord.second), data.stateVal}
            );
        }
    }
    return intersections;
}


bool Scene::checkOverlap(int draggedComponentId) const
{
    auto it = m_componentViews.find(draggedComponentId);
    if (it == m_componentViews.end())
        return false;
    ComponentView* dragged = it->second.get();

    GridCoords draggedPos = dragged->getGridPosition();

    // Collect all absolute pin positions for the dragged component
    std::vector<GridCoords> draggedPins;
    for (const auto& pin : dragged->getInputPins())
        draggedPins.push_back(dragged->getAbsolutePinGridPos(pin));
    for (const auto& pin : dragged->getOutputPins())
        draggedPins.push_back(dragged->getAbsolutePinGridPos(pin));

    for (const auto& [id, other] : m_componentViews)
    {
        if (id == draggedComponentId)
            continue; // Don't check against itself

        // 1. Check Origin vs Origin
        if (draggedPos == other->getGridPosition())
            return true;

        // 2. Check Pin vs Pin
        for (const auto& otherPin : other->getInputPins())
        {
            GridCoords p = other->getAbsolutePinGridPos(otherPin);
            for (const auto& dp : draggedPins)
                if (dp == p)
                    return true;
        }
        for (const auto& otherPin : other->getOutputPins())
        {
            GridCoords p = other->getAbsolutePinGridPos(otherPin);
            for (const auto& dp : draggedPins)
                if (dp == p)
                    return true;
        }
    }

    return false; // No overlaps found
}


int Scene::addLatch(LatchType type, GridCoords gridPos)
{
    int id = m_circuit.addLatch(type);
    glm::vec2 size = {0.30f, 0.20f}; // 6 x 4 grid cells, the footprint the pins span

    if (type == LatchType::SR_LATCH)
    {
        std::vector<PinUI> inPins = {
            {PinType::INPUT, 0, PinState::DISCONNECTED, {-3, 1}}, // S
            {PinType::INPUT, 1, PinState::DISCONNECTED, {-3, -1}} // R
        };
        std::vector<PinUI> outPins = {
            {PinType::OUTPUT, 0, PinState::DISCONNECTED, {3, 1}}, // Q
            {PinType::OUTPUT, 1, PinState::DISCONNECTED, {3, -1}} // ~Q
        };
        m_componentViews.emplace(
            id,
            std::make_unique<LatchView>(
                gridPos,
                id,
                size,
                "latch",
                "SR LATCH",
                inPins,
                outPins,
                std::vector<std::string>{"S", "R"},
                std::vector<std::string>{"Q", "~Q"}
            )
        );
    }
    else
    {
        std::vector<PinUI> inPins = {
            {PinType::INPUT, 0, PinState::DISCONNECTED, {-3, 1}}, // D
            {PinType::INPUT, 1, PinState::DISCONNECTED, {-3, -1}} // E
        };
        std::vector<PinUI> outPins = {
            {PinType::OUTPUT, 0, PinState::DISCONNECTED, {3, 1}}, // Q
            {PinType::OUTPUT, 1, PinState::DISCONNECTED, {3, -1}} // ~Q
        };
        m_componentViews.emplace(
            id,
            std::make_unique<LatchView>(
                gridPos,
                id,
                size,
                "latch",
                "D LATCH",
                inPins,
                outPins,
                std::vector<std::string>{"D", "E"},
                std::vector<std::string>{"Q", "~Q"}
            )
        );
    }

    rebuildNets();
    return id;
}