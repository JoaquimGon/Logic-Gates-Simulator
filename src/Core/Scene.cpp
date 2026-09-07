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

    for (auto& wire : m_wires) {
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

size_t Scene::commitWire(Wire wire)
{
    m_wires.push_back(std::move(wire));
    return m_wires.size() - 1;
}

Wire Scene::extractWire(size_t index)
{
    Wire w = m_wires[index];
    m_wires.erase(m_wires.begin() + index);
    return w;
}

bool Scene::splitWireAt(size_t index, GridCoords point, Wire& outA, Wire& outB)
{
    if (index >= m_wires.size()) return false;
    if (!m_wires[index].splitAt(point, outA, outB)) return false;
    m_wires.erase(m_wires.begin() + index);
    return true;
}

void Scene::addWires(Wire a, Wire b)
{
    m_wires.push_back(std::move(a));
    m_wires.push_back(std::move(b));
}

void Scene::removeWire(size_t index)
{
    if (index < m_wires.size()) m_wires.erase(m_wires.begin() + index);
}

bool Scene::connectPins(int srcComponentId, int destComponentId, int destPinIndex)
{
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

        for (size_t i = 0; i < m_wires.size() && !handled; ++i) {
            Wire& wire = m_wires[i];
            if (wire.getPath().empty()) continue;

            if (!wire.hasDest() && (wire.getPath().front() == pinPos || wire.getPath().back() == pinPos)) {
                wire.setDest(componentId, static_cast<int>(pin.pin_index));
                if (wire.hasSource()) connectPins(wire.getSource().componentId, componentId, static_cast<int>(pin.pin_index));
                handled = true;
            }
            else if (wire.containsPoint(pinPos)) {
                Wire wireA, wireB;
                if (splitWireAt(i, pinPos, wireA, wireB)) {
                    wireA.setDest(componentId, static_cast<int>(pin.pin_index));
                    if (wireA.hasSource()) connectPins(wireA.getSource().componentId, componentId, static_cast<int>(pin.pin_index));
                    addWires(wireA, wireB);
                }
                handled = true;
            }
        }
    }

    // Output pin(s): look for a wire needing a source
    for (const auto& pin : component->getOutputPins()) {
        GridCoords pinPos = component->getAbsolutePinGridPos(pin);
        bool handled = false;

        for (size_t i = 0; i < m_wires.size() && !handled; ++i) {
            Wire& wire = m_wires[i];
            if (wire.getPath().empty()) continue;

            if (!wire.hasSource() && (wire.getPath().front() == pinPos || wire.getPath().back() == pinPos)) {
                wire.setSource(componentId, static_cast<int>(pin.pin_index));
                if (wire.hasDest()) connectPins(componentId, wire.getDest().componentId, wire.getDest().pinIndex);
                handled = true;
            }
            else if (wire.containsPoint(pinPos)) {
                Wire wireA, wireB;
                if (splitWireAt(i, pinPos, wireA, wireB)) {
                    wireB.setSource(componentId, static_cast<int>(pin.pin_index));
                    if (wireB.hasDest()) connectPins(componentId, wireB.getDest().componentId, wireB.getDest().pinIndex);
                    addWires(wireA, wireB);
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
    int matchedWireIndex = -1;
    bool matchedIsStart = false;

    for (size_t i = 0; i < m_wires.size(); ++i) {
        const auto& path = m_wires[i].getPath();
        if (path.empty()) continue;

        bool isStart = (gridPos == path.front());
        bool isEnd = (path.size() > 1 && gridPos == path.back());

        if (isStart || isEnd) {
            endpointMatches++;
            if (matchedWireIndex == -1) {
                matchedWireIndex = static_cast<int>(i);
                matchedIsStart = isStart;
            }
        }
    }

    if (endpointMatches >= 2) {
        return { HitType::WIRE_JUNCTION, -1, -1, PinType::INPUT, -1 };
    }
    if (endpointMatches == 1) {
        return { matchedIsStart ? HitType::WIRE_START : HitType::WIRE_END,
                  -1, -1, PinType::INPUT, matchedWireIndex };
    }

    for (size_t i = 0; i < m_wires.size(); ++i) {
        if (m_wires[i].containsPoint(gridPos)) {
            return { HitType::WIRE_BODY, -1, -1, PinType::INPUT, static_cast<int>(i) };
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

GridCoords Scene::clipSegmentAgainstWires(GridCoords from, GridCoords to) const
{
    GridCoords bestEntry = to;
    int bestDist = std::abs(to.x - from.x) + std::abs(to.y - from.y);

    for (const auto& wire : m_wires) {
        const auto& path = wire.getPath();
        if (path.size() < 2) continue;

        for (size_t i = 0; i + 1 < path.size(); ++i) {
            GridCoords entry;
            if (segmentsOverlapCollinearly(from, to, path[i], path[i + 1], entry)) {
                int dist = std::abs(entry.x - from.x) + std::abs(entry.y - from.y);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestEntry = entry;
                }
            }
        }
    }
    return bestEntry;
}

void Scene::propagate()
{
    m_circuit.propagate();
}

void Scene::syncVisuals()
{
    // Evaluate logic components
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

    // Set source wires
    for (auto& wire : m_wires) {
        if (wire.hasSource()) {
            if (Component* src = m_circuit.getComponent(wire.getSource().componentId))
                wire.setState(src->getStateOutPin(wire.getSource().pinIndex) ? PinState::ON : PinState::OFF);
            else wire.setState(PinState::DISCONNECTED);
        }
        else {
            wire.setState(PinState::DISCONNECTED);
        }
    }

    // Robust Topological Flood Fill
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < m_wires.size(); ++i) {
            PinState stateI = m_wires[i].getState();
            if (stateI == PinState::DISCONNECTED) continue;

            for (size_t j = 0; j < m_wires.size(); ++j) {
                if (i == j) continue;
                PinState stateJ = m_wires[j].getState();
                
                // Priority: ON > OFF > DISCONNECTED
                bool shouldPropagate = (stateI == PinState::ON && (stateJ == PinState::DISCONNECTED || stateJ == PinState::OFF)) ||
                                       (stateI == PinState::OFF && stateJ == PinState::DISCONNECTED);

                if (!shouldPropagate) continue;

                // Only propagate if the wires explicitly share an endpoint!
                bool touches = false;
                const auto& pathI = m_wires[i].getPath();
                const auto& pathJ = m_wires[j].getPath();

                if (!pathI.empty() && !pathJ.empty()) {
                    GridCoords iStart = pathI.front(), iEnd = pathI.back();
                    GridCoords jStart = pathJ.front(), jEnd = pathJ.back();

                    if (iStart == jStart || iStart == jEnd || iEnd == jStart || iEnd == jEnd) {
                        touches = true;
                    }
                }

                if (touches) {
                    m_wires[j].setState(stateI);
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

    for (const auto& wire : m_wires) {
        const auto& path = wire.getPath();
        if (path.empty()) continue;

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
        if (data.count >= 3) {
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