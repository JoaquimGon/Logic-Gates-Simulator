#include "Scene.h"

namespace {
    // Checks if two axis-aligned segments [a,b] and [c,d] lie on the same line and
    // overlap over more than a single point. If so, outEntry is set to the point on
    // [a,b] closest to 'a' where that overlap begins.
    bool segmentsOverlapCollinearly(GridCoords a, GridCoords b, GridCoords c, GridCoords d, GridCoords& outEntry)
    {
        bool abHorizontal = (a.y == b.y);
        bool abVertical = (a.x == b.x);
        bool cdHorizontal = (c.y == d.y);
        bool cdVertical = (c.x == d.x);

        if (abHorizontal && cdHorizontal && a.y == c.y) {
            int aMin = std::min(a.x, b.x), aMax = std::max(a.x, b.x);
            int cMin = std::min(c.x, d.x), cMax = std::max(c.x, d.x);
            int overlapMin = std::max(aMin, cMin);
            int overlapMax = std::min(aMax, cMax);
            if (overlapMax - overlapMin >= 1) {
                outEntry = { (a.x <= b.x) ? overlapMin : overlapMax, a.y };
                return true;
            }
        }
        else if (abVertical && cdVertical && a.x == c.x) {
            int aMin = std::min(a.y, b.y), aMax = std::max(a.y, b.y);
            int cMin = std::min(c.y, d.y), cMax = std::max(c.y, d.y);
            int overlapMin = std::max(aMin, cMin);
            int overlapMax = std::min(aMax, cMax);
            if (overlapMax - overlapMin >= 1) {
                outEntry = { a.x, (a.y <= b.y) ? overlapMin : overlapMax };
                return true;
            }
        }
        return false;
    }
}


int Scene::addGate(GateType type, GridCoords gridPos, glm::vec2 size, const std::string& shaderName,
    std::vector<PinUI> inputs, std::vector<PinUI> outputs)
{
    int id = m_circuit.addGate(type);
    m_componentViews.emplace(id, std::make_unique<GateView>(gridPos, id, size, shaderName, std::move(inputs), std::move(outputs)));
    return id;
}

int Scene::addInputPin(GridCoords gridPos, glm::vec2 size, const std::string& shaderName, bool initialState)
{
    int id = m_circuit.addInputPin(initialState);
    m_componentViews.emplace(id, std::make_unique<InputPinView>(gridPos, id, size, shaderName));
    return id;
}

void Scene::removeComponent(int componentId)
{
    m_circuit.delComponent(componentId);
    m_componentViews.erase(componentId);

    for (auto& [id, wire] : m_wires) {
        if (wire.hasSource() && wire.getSource().componentId == componentId) wire.disconnectSource();
        if (wire.hasDest() && wire.getDest().componentId == componentId) wire.disconnectDest();
    }
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
    // Ids are handed out in order and never recycled: that is what makes a cached
    // WireId keep referring to the same wire for the rest of the session.
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
    for (const auto& [id, wire] : m_wires) ids.push_back(id);
    return ids;
}

std::optional<WireId> Scene::commitWire(Wire wire)
{
    wire.simplifyPath();
    if (wire.getPath().size() < 2) {
        // A one-point wire is neither drawable nor electrical, so it is never stored.
        return std::nullopt;
    }

    // Normalize path orientation so front() is the source side.
    // If the wire was drawn starting from an input pin (sink -> source or sink -> empty space),
    // it holds a destination, and its first point sits exactly on that destination pin.
    if (wire.hasDest()) {
        const auto& dest = wire.getDest();

        auto viewIt = m_componentViews.find(dest.componentId);
        if (viewIt != m_componentViews.end()) {
            ComponentView* view = viewIt->second.get();

            for (const auto& pin : view->getInputPins()) {
                if (pin.pin_index == dest.pinIndex) {
                    GridCoords destPos = view->getAbsolutePinGridPos(pin);
                    if (wire.getPath().front() == destPos) {
                        std::vector<GridCoords> reversedPath = wire.getPath();
                        std::reverse(reversedPath.begin(), reversedPath.end());
                        wire.setPath(reversedPath);
                    }
                    break;
                }
            }
        }
    }

    return insertWire(std::move(wire));
}

std::optional<Wire> Scene::extractWire(WireId id)
{
    auto it = m_wires.find(id);
    if (it == m_wires.end()) return std::nullopt;

    Wire wire = std::move(it->second);
    m_wires.erase(it);
    return wire;
}

bool Scene::splitWireAt(WireId id, GridCoords point, Wire& outA, Wire& outB)
{
    Wire* wire = getWire(id);
    if (!wire) return false;
    if (!wire->splitAt(point, outA, outB)) return false;

    m_wires.erase(id);
    return true;
}

std::pair<WireId, WireId> Scene::addWires(Wire a, Wire b)
{
    return { insertWire(std::move(a)), insertWire(std::move(b)) };
}

bool Scene::removeWire(WireId id)
{
    return m_wires.erase(id) > 0;
}

bool Scene::connectPins(int srcComponentId, int destComponentId, int destPinIndex)
{
    if (srcComponentId == destComponentId) return false;
    return m_circuit.connectComponents(srcComponentId, destComponentId, destPinIndex);
}

void Scene::disconnectPins(int srcComponentId, int destComponentId, int destPinIndex)
{
    m_circuit.disconnectComponents(srcComponentId, destComponentId, destPinIndex);
}

void Scene::reconnectWiresToComponent(int componentId)
{
    ComponentView* component = getComponentView(componentId);
    if (!component) return;

    // Input pins: look for a wire needing a destination
    for (const auto& pin : component->getInputPins()) {
        GridCoords pinPos = component->getAbsolutePinGridPos(pin);
        bool handled = false;

        // Snapshot the ids: a split below reshapes the container, and an id consumed by
        // that split simply resolves to nullptr on the next lookup.
        for (WireId wireId : getWireIds()) {
            if (handled) break;
            Wire* wire = getWire(wireId);
            if (!wire || wire->getPath().empty()) continue;

            // Never attach an input to a wire sourced by the same component.
            if (wire->hasSource() && wire->getSource().componentId == componentId) continue;

            const bool atEndpoint = wire->getPath().front() == pinPos || wire->getPath().back() == pinPos;
            if (atEndpoint) {
                if (!wire->hasDest()) {
                    wire->setDest(componentId, static_cast<int>(pin.pin_index));
                    if (wire->hasSource()) {
                        if (!connectPins(wire->getSource().componentId, componentId, static_cast<int>(pin.pin_index))) {
                            wire->disconnectDest();
                        }
                    }
                    handled = true;
                }
                else if (wire->getDest().componentId == componentId &&
                    wire->getDest().pinIndex == static_cast<int>(pin.pin_index)) {
                    handled = true;
                }
                continue;
            }

            if (wire->containsPoint(pinPos)) {
                Wire wireA, wireB;
                if (splitWireAt(wireId, pinPos, wireA, wireB)) {
                    wireA.setDest(componentId, static_cast<int>(pin.pin_index));
                    if (wireA.hasSource()) {
                        if (!connectPins(wireA.getSource().componentId, componentId, static_cast<int>(pin.pin_index))) {
                            wireA.disconnectDest();
                        }
                    }
                    addWires(std::move(wireA), std::move(wireB));
                }
                handled = true;
            }
        }
    }

    // Output pin(s): look for a wire needing a source
    for (const auto& pin : component->getOutputPins()) {
        GridCoords pinPos = component->getAbsolutePinGridPos(pin);
        bool handled = false;

        for (WireId wireId : getWireIds()) {
            if (handled) break;
            Wire* wire = getWire(wireId);
            if (!wire || wire->getPath().empty()) continue;

            // Never attach an output to a wire ending at the same component.
            if (wire->hasDest() && wire->getDest().componentId == componentId) continue;

            const bool atEndpoint = wire->getPath().front() == pinPos || wire->getPath().back() == pinPos;
            if (atEndpoint) {
                if (!wire->hasSource()) {
                    wire->setSource(componentId, static_cast<int>(pin.pin_index));
                    if (wire->hasDest()) {
                        if (!connectPins(componentId, wire->getDest().componentId, wire->getDest().pinIndex)) {
                            wire->disconnectDest();
                        }
                    }
                    handled = true;
                }
                else if (wire->getSource().componentId == componentId &&
                    wire->getSource().pinIndex == static_cast<int>(pin.pin_index)) {
                    handled = true;
                }
                continue;
            }

            if (wire->containsPoint(pinPos)) {
                Wire wireA, wireB;
                if (splitWireAt(wireId, pinPos, wireA, wireB)) {
                    wireB.setSource(componentId, static_cast<int>(pin.pin_index));
                    if (wireB.hasDest()) 
                        if (!connectPins(componentId, wireB.getDest().componentId, wireB.getDest().pinIndex)) {
                            wireB.disconnectDest();
                        }
                    addWires(std::move(wireA), std::move(wireB));
                }
                handled = true;
            }
        }
    }
}

HitResult Scene::hitTest(glm::vec2 worldPos, GridCoords gridPos) const
{
    // 1. Pins — smallest, most specific targets, checked first
    for (const auto& [id, component] : m_componentViews) {
        for (const auto& pin : component->getInputPins())
            if (gridPos == component->getAbsolutePinGridPos(pin))
                return { HitType::COMPONENT_PIN, id, static_cast<int>(pin.pin_index), PinType::INPUT, -1 };

        for (const auto& pin : component->getOutputPins())
            if (gridPos == component->getAbsolutePinGridPos(pin))
                return { HitType::COMPONENT_PIN, id, static_cast<int>(pin.pin_index), PinType::OUTPUT, -1 };
    }

    // 2. Wire endpoints / bodies / junctions 
    int endpointMatches = 0;
    WireId matchedWireId = INVALID_WIRE_ID;
    bool matchedIsStart = false;

    for (const auto& [id, wire] : m_wires) {
        const auto& path = wire.getPath();
        if (path.empty()) continue;

        bool isStart = (gridPos == path.front());
        bool isEnd = (path.size() > 1 && gridPos == path.back());

        if (isStart || isEnd) {
            endpointMatches++;
            if (matchedWireId == INVALID_WIRE_ID) {
                matchedWireId = id;
                matchedIsStart = isStart;
            }
        }
    }

    if (endpointMatches >= 2) {
        return { HitType::WIRE_JUNCTION, -1, -1, PinType::INPUT, matchedWireId };
    }
    if (endpointMatches == 1) {
        return { matchedIsStart ? HitType::WIRE_START : HitType::WIRE_END,
                  -1, -1, PinType::INPUT, matchedWireId };
    }

    for (const auto& [id, wire] : m_wires) {
        if (wire.containsPoint(gridPos)) {
            return { HitType::WIRE_BODY, -1, -1, PinType::INPUT, id };
        }
    }


    for (const auto& [id, component] : m_componentViews) {


        glm::vec2 halfSize = (component->getSize() * 0.5f) - glm::vec2(0.015f);

        glm::vec2 delta = worldPos - component->getPosition();
        if (std::abs(delta.x) <= halfSize.x && std::abs(delta.y) <= halfSize.y)
            return { HitType::COMPONENT_BODY, id, -1, PinType::INPUT, -1 };
    }

    return {};
}

bool Scene::getCollinearOverlap(GridCoords a, GridCoords b, GridCoords c, GridCoords d, GridCoords& outStart, GridCoords& outEnd) const
{
    bool abHorizontal = (a.y == b.y), abVertical = (a.x == b.x);
    bool cdHorizontal = (c.y == d.y), cdVertical = (c.x == d.x);

    if (abHorizontal && cdHorizontal && a.y == c.y) {
        int aMin = std::min(a.x, b.x), aMax = std::max(a.x, b.x);
        int cMin = std::min(c.x, d.x), cMax = std::max(c.x, d.x);
        int oMin = std::max(aMin, cMin), oMax = std::min(aMax, cMax);
        if (oMax > oMin) {
            if (std::abs(a.x - oMin) < std::abs(a.x - oMax)) { outStart = { oMin, a.y }; outEnd = { oMax, a.y }; }
            else { outStart = { oMax, a.y }; outEnd = { oMin, a.y }; }
            return true;
        }
    }
    else if (abVertical && cdVertical && a.x == c.x) {
        int aMin = std::min(a.y, b.y), aMax = std::max(a.y, b.y);
        int cMin = std::min(c.y, d.y), cMax = std::max(c.y, d.y);
        int oMin = std::max(aMin, cMin), oMax = std::min(aMax, cMax);
        if (oMax > oMin) {
            if (std::abs(a.y - oMin) < std::abs(a.y - oMax)) { outStart = { a.x, oMin }; outEnd = { a.x, oMax }; }
            else { outStart = { a.x, oMax }; outEnd = { a.x, oMin }; }
            return true;
        }
    }
    return false;
}

void Scene::forceEndpointAt(GridCoords p) {
    // The ids are snapshotted because splitting replaces the wire being processed and
    // appends the two fragments: those fragments must not be re-examined in this pass,
    // and ids consumed by an earlier split resolve to nullptr below.
    for (WireId id : getWireIds()) {
        Wire* wire = getWire(id);
        if (!wire) continue;
        if (!wire->containsPoint(p)) continue;
        if (wire->getPath().front() == p || wire->getPath().back() == p) continue;

        Wire wA, wB;
        if (splitWireAt(id, p, wA, wB)) {
            addWires(std::move(wA), std::move(wB));
        }
    }
}

void Scene::healWires()
{
    // One-point wires have no drawable segment and no electrical meaning. Old
    // endpoint splits could leave them behind and make the intersection counter
    // report a junction that did not really exist.
    std::erase_if(m_wires, [](const std::pair<const WireId, Wire>& entry) {
        return entry.second.getPath().size() < 2;
        });

    bool changed = true;
    while (changed) {
        changed = false;

        struct Endpt {
            std::vector<std::pair<WireId, bool>> wires; // <wireId, isStart>
            bool hasPin = false;
        };
        std::map<std::pair<int, int>, Endpt> pointMap;

        // 1. Mark all component pins as "unhealable" anchor points
        for (const auto& [id, comp] : m_componentViews) {
            for (const auto& pin : comp->getInputPins()) {
                auto p = comp->getAbsolutePinGridPos(pin);
                pointMap[{p.x, p.y}].hasPin = true;
            }
            for (const auto& pin : comp->getOutputPins()) {
                auto p = comp->getAbsolutePinGridPos(pin);
                pointMap[{p.x, p.y}].hasPin = true;
            }
        }

        // 2. Tally all wire endpoints
        for (const auto& [id, wire] : m_wires) {
            const auto& path = wire.getPath();
            if (path.size() >= 2) {
                pointMap[{path.front().x, path.front().y}].wires.push_back({ id, true });
                pointMap[{path.back().x, path.back().y}].wires.push_back({ id, false });
            }
        }

        // 3. Heal exactly-2 junctions
        for (const auto& [coord, data] : pointMap) {
            if (data.hasPin || data.wires.size() != 2) continue; // Skip pins, dead ends, and 3-way/4-way junctions

            WireId w1Id = data.wires[0].first;
            bool w1IsStart = data.wires[0].second;
            WireId w2Id = data.wires[1].first;
            bool w2IsStart = data.wires[1].second;

            if (w1Id == w2Id) continue; // Ignore a wire looping back onto itself

            Wire* w1 = getWire(w1Id);
            Wire* w2 = getWire(w2Id);
            if (!w1 || !w2) continue; // Already consumed by an earlier merge in this pass

            // Reconstruct a unified geometric path
            std::vector<GridCoords> newPath;
            if (!w1IsStart) {
                // w1 ends at P. Append w2 to w1.
                newPath = w1->getPath();
                std::vector<GridCoords> p2 = w2->getPath();
                if (!w2IsStart) std::reverse(p2.begin(), p2.end()); // Orient w2 so it starts at P
                newPath.insert(newPath.end(), p2.begin() + 1, p2.end());
            }
            else {
                // w1 starts at P. Prepend w1 to w2.
                newPath = w1->getPath();
                std::reverse(newPath.begin(), newPath.end()); // Flip w1 so it ends at P
                std::vector<GridCoords> p2 = w2->getPath();
                if (!w2IsStart) std::reverse(p2.begin(), p2.end());
                newPath.insert(newPath.end(), p2.begin() + 1, p2.end());
            }

            // Create the newly merged wire
            Wire mergedWire;
            mergedWire.setPath(newPath);

            // Safely inherit logic connections (since they form a continuous physical line, they share electrical states)
            WireEndpoint src = w1->hasSource() ? w1->getSource() : (w2->hasSource() ? w2->getSource() : WireEndpoint());
            WireEndpoint dst = w1->hasDest() ? w1->getDest() : (w2->hasDest() ? w2->getDest() : WireEndpoint());
            if (src.isConnected()) mergedWire.setSource(src.componentId, src.pinIndex);
            if (dst.isConnected()) mergedWire.setDest(dst.componentId, dst.pinIndex);

            mergedWire.setState(w1->getState());
            mergedWire.simplifyPath(); // MAGIC: If they were collinear, this instantly drops the seam!

            // Erase the two fragments by id. No index shifting to compensate for, and the
            // healed wire takes a fresh id, so nothing that cached the old ids can be
            // silently repointed at the merged wire.
            m_wires.erase(w1Id);
            m_wires.erase(w2Id);

            // Store the healed wire and restart the pass
            insertWire(std::move(mergedWire));
            changed = true;
            break;
        }
    }

    bool logicChanged = true;
    while (logicChanged) {
        logicChanged = false;

        // Raw pointers, not ids: nothing is inserted or erased during this pass, so they
        // stay valid, and this loop reaches into the container O(n^2) times.
        std::vector<Wire*> wires;
        wires.reserve(m_wires.size());
        for (auto& [wireId, wire] : m_wires) wires.push_back(&wire);

        for (size_t i = 0; i < wires.size(); ++i) {
            for (size_t j = i + 1; j < wires.size(); ++j) {
                const auto& pathI = wires[i]->getPath();
                const auto& pathJ = wires[j]->getPath();
                if (pathI.empty() || pathJ.empty()) continue;

                GridCoords iStart = pathI.front(), iEnd = pathI.back();
                GridCoords jStart = pathJ.front(), jEnd = pathJ.back();

                // If the geometry physically touches at a topological junction...
                if (iStart == jStart || iStart == jEnd || iEnd == jStart || iEnd == jEnd) {

                    // Share Sources
                    if (wires[i]->hasSource() && !wires[j]->hasSource()) {
                        const auto source = wires[i]->getSource();
                        if (!wires[j]->hasDest() || wires[j]->getDest().componentId != source.componentId) {
                            wires[j]->setSource(source.componentId, source.pinIndex);
                            logicChanged = true;
                        }
                    }
                    else if (!wires[i]->hasSource() && wires[j]->hasSource()) {
                        const auto source = wires[j]->getSource();
                        if (!wires[i]->hasDest() || wires[i]->getDest().componentId != source.componentId) {
                            wires[i]->setSource(source.componentId, source.pinIndex);
                            logicChanged = true;
                        }
                    }

                    // Share Destinations
                    if (wires[i]->hasDest() && !wires[j]->hasDest()) {
                        const auto dest = wires[i]->getDest();
                        if (!wires[j]->hasSource() || wires[j]->getSource().componentId != dest.componentId) {
                            wires[j]->setDest(dest.componentId, dest.pinIndex);
                            logicChanged = true;
                        }
                    }
                    else if (!wires[i]->hasDest() && wires[j]->hasDest()) {
                        const auto dest = wires[j]->getDest();
                        if (!wires[i]->hasSource() || wires[i]->getSource().componentId != dest.componentId) {
                            wires[i]->setDest(dest.componentId, dest.pinIndex);
                            logicChanged = true;
                        }
                    }
                }
            }
        }
    }

    // 5. Force the Circuit engine to execute all valid, completed networks
    for (auto& [id, wire] : m_wires) {
        if (wire.hasSource() && wire.hasDest() &&
            wire.getSource().componentId != wire.getDest().componentId) {
            // Safe to call redundantly; Circuit::connectComponents returns false if already mapped
            if (!connectPins(wire.getSource().componentId, wire.getDest().componentId, wire.getDest().pinIndex))
            {
                wire.disconnectDest();
            }
        }
    }
}


EvalOrderResult Scene::propagate()
{
    return m_circuit.propagate();
}

void Scene::syncVisuals()
{
    for (auto& [id, view] : m_componentViews) {
        Component* comp = m_circuit.getComponent(id);
        if (!comp) continue;

        auto& outputs = view->getOutputPins();
        for (size_t i = 0; i < outputs.size(); ++i)
            outputs[i].state = comp->getStateOutPin(static_cast<int>(i)) ? PinState::ON : PinState::OFF;

        auto inSignals = comp->getStateInPins();
        auto& inputs = view->getInputPins();
        for (size_t i = 0; i < inSignals.size() && i < inputs.size(); ++i)
            inputs[i].state = inSignals[i] ? PinState::ON : PinState::OFF;
    }

    for (auto& [id, wire] : m_wires) {
        if (wire.hasSource()) {
            if (Component* src = m_circuit.getComponent(wire.getSource().componentId))
                wire.setState(src->getStateOutPin(wire.getSource().pinIndex) ? PinState::ON : PinState::OFF);
            else wire.setState(PinState::DISCONNECTED);
        }
        else {
            wire.setState(PinState::DISCONNECTED);
        }
    }

    // Pointers rather than ids in this pass: nothing is inserted or erased here, so they
    // stay valid, and the fixpoint below would otherwise do O(n^2) map lookups.
    std::vector<Wire*> wires;
    wires.reserve(m_wires.size());
    for (auto& [id, wire] : m_wires) wires.push_back(&wire);

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < wires.size(); ++i) {
            PinState stateI = wires[i]->getState();
            if (stateI == PinState::DISCONNECTED) continue;

            for (size_t j = 0; j < wires.size(); ++j) {
                if (i == j) continue;
                PinState stateJ = wires[j]->getState();

                bool shouldPropagate = (stateI == PinState::ON && (stateJ == PinState::DISCONNECTED || stateJ == PinState::OFF)) ||
                    (stateI == PinState::OFF && stateJ == PinState::DISCONNECTED);

                if (!shouldPropagate) continue;

                bool touches = false;
                const auto& pathI = wires[i]->getPath();
                const auto& pathJ = wires[j]->getPath();

                if (!pathI.empty() && !pathJ.empty()) {
                    GridCoords iStart = pathI.front(), iEnd = pathI.back();
                    GridCoords jStart = pathJ.front(), jEnd = pathJ.back();
                    if (iStart == jStart || iStart == jEnd || iEnd == jStart || iEnd == jEnd) touches = true;
                }

                if (!touches) {
                    for (size_t a = 0; a + 1 < pathI.size(); ++a) {
                        for (size_t b = 0; b + 1 < pathJ.size(); ++b) {
                            GridCoords dummy;
                            if (segmentsOverlapCollinearly(pathI[a], pathI[a + 1], pathJ[b], pathJ[b + 1], dummy)) {
                                touches = true; break;
                            }
                        }
                        if (touches) break;
                    }
                }

                if (touches) {
                    wires[j]->setState(stateI);
                    changed = true;
                }
            }
        }
    }
}

bool Scene::handleClick(int componentId)
{
    ComponentView* view = getComponentView(componentId);
    if (view) {
        // This dynamically routes to either GateView (returns false) 
        // or InputPinView (toggles state and returns true)
        return view->onClick(m_circuit);
    }
    return false;
}

std::vector<glm::vec3> Scene::getWireIntersections() const
{
    std::vector<glm::vec3> intersections;
    struct PointData { int count = 0; float stateVal = 0.0f; };
    std::map<std::pair<int, int>, PointData> endpointMap;
    std::map<std::pair<int, int>, bool> componentPinPositions;

    for (const auto& [id, component] : m_componentViews) {
        for (const auto& pin : component->getInputPins()) {
            GridCoords pos = component->getAbsolutePinGridPos(pin);
            componentPinPositions[{ pos.x, pos.y }] = true;
        }
        for (const auto& pin : component->getOutputPins()) {
            GridCoords pos = component->getAbsolutePinGridPos(pin);
            componentPinPositions[{ pos.x, pos.y }] = true;
        }
    }

    for (const auto& [id, wire] : m_wires) {
        const auto& path = wire.getPath();
        if (path.size() < 2) continue;

        float stateVal = 0.0f; // DISCONNECTED
        if (wire.getState() == PinState::ON) stateVal = 1.0f;
        else if (wire.getState() == PinState::OFF) stateVal = 2.0f;

        // Register START endpoint
        auto startCoord = std::make_pair(path.front().x, path.front().y);
        endpointMap[startCoord].count++;
        if (stateVal == 1.0f || (stateVal == 2.0f && endpointMap[startCoord].stateVal == 0.0f))
            endpointMap[startCoord].stateVal = stateVal; // Priority: ON > OFF > DISCONNECTED

        // Register END endpoint
        if (path.size() > 1) {
            auto endCoord = std::make_pair(path.back().x, path.back().y);
            endpointMap[endCoord].count++;
            if (stateVal == 1.0f || (stateVal == 2.0f && endpointMap[endCoord].stateVal == 0.0f))
                endpointMap[endCoord].stateVal = stateVal;
        }
    }

    // Dot only appears if 3 or more topological endpoints meet here!
    for (const auto& [coord, data] : endpointMap) {
        // A component pin already has its own visual marker. It is a terminal,
        // not a free-standing wire junction, even when several wire fragments
        // share its coordinates.
        if (data.count >= 3 && componentPinPositions.find(coord) == componentPinPositions.end()) {
            intersections.push_back({ static_cast<float>(coord.first), static_cast<float>(coord.second), data.stateVal });
        }
    }
    return intersections;
}


bool Scene::checkOverlap(int draggedComponentId) const
{
    auto it = m_componentViews.find(draggedComponentId);
    if (it == m_componentViews.end()) return false;
    ComponentView* dragged = it->second.get();

    GridCoords draggedPos = dragged->getGridPosition();

    // Collect all absolute pin positions for the dragged component
    std::vector<GridCoords> draggedPins;
    for (const auto& pin : dragged->getInputPins()) draggedPins.push_back(dragged->getAbsolutePinGridPos(pin));
    for (const auto& pin : dragged->getOutputPins()) draggedPins.push_back(dragged->getAbsolutePinGridPos(pin));

    for (const auto& [id, other] : m_componentViews) {
        if (id == draggedComponentId) continue; // Don't check against itself

        // 1. Check Origin vs Origin
        if (draggedPos == other->getGridPosition()) return true;

        // 2. Check Pin vs Pin
        for (const auto& otherPin : other->getInputPins()) {
            GridCoords p = other->getAbsolutePinGridPos(otherPin);
            for (const auto& dp : draggedPins) if (dp == p) return true;
        }
        for (const auto& otherPin : other->getOutputPins()) {
            GridCoords p = other->getAbsolutePinGridPos(otherPin);
            for (const auto& dp : draggedPins) if (dp == p) return true;
        }
    }

    return false; // No overlaps found
}