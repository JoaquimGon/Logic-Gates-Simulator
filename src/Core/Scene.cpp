#include "Scene.h"
#include <cmath>

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

    // 2. Wire endpoints / bodies
    for (size_t i = 0; i < m_wires.size(); ++i) {
        const auto& path = m_wires[i].getPath();
        if (path.empty()) continue;

        if (gridPos == path.back())  return { HitType::WIRE_END,   -1, -1, PinType::INPUT, static_cast<int>(i) };
        if (gridPos == path.front()) return { HitType::WIRE_START, -1, -1, PinType::INPUT, static_cast<int>(i) };
        if (m_wires[i].containsPoint(gridPos)) return { HitType::WIRE_BODY, -1, -1, PinType::INPUT, static_cast<int>(i) };
    }

    // 3. Component bodies — world-space AABB
    for (const auto& [id, component] : m_componentViews) {


        glm::vec2 halfSize = (component->getSize() * 0.5f) - glm::vec2(0.015f);

        glm::vec2 delta = worldPos - component->getPosition();
        if (std::abs(delta.x) <= halfSize.x && std::abs(delta.y) <= halfSize.y)
            return { HitType::COMPONENT_BODY, id, -1, PinType::INPUT, -1 };
    }

    return {};
}

void Scene::propagate()
{
    m_circuit.propagate();
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

    // Reset all wires and evaluate explicit sources first
    for (auto& wire : m_wires) {
        if (wire.hasSource()) {
            if (Component* src = m_circuit.getComponent(wire.getSource().componentId))
                wire.setState(src->getStateOutPin(wire.getSource().pinIndex) ? PinState::ON : PinState::OFF);
            else
                wire.setState(PinState::DISCONNECTED);
        }
        else {
            wire.setState(PinState::DISCONNECTED);
        }
    }

    // NEW: Flood-fill state to physically touching networks!
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < m_wires.size(); ++i) {
            if (m_wires[i].getState() == PinState::DISCONNECTED) continue;

            for (size_t j = 0; j < m_wires.size(); ++j) {
                if (i == j || m_wires[j].getState() == m_wires[i].getState()) continue;

                // Check if paths intersect physically
                bool touches = false;
                for (const auto& p1 : m_wires[i].getPath()) {
                    for (const auto& p2 : m_wires[j].getPath()) {
                        if (p1 == p2) { touches = true; break; }
                    }
                    if (touches) break;
                }

                if (touches && m_wires[j].getState() == PinState::DISCONNECTED) {
                    m_wires[j].setState(m_wires[i].getState());
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

    struct PointData {
        int count = 0;
        float stateVal = 0.0f;
    };

    std::map<std::pair<int, int>, PointData> endpointMap;

    for (const auto& wire : m_wires) {
        const auto& path = wire.getPath();
        if (path.empty()) continue;

        // Map state to colors (0=DISCONNECTED, 1=ON, 2=OFF)
        float stateVal = 0.0f;
        if (wire.getState() == PinState::ON) stateVal = 1.0f;
        else if (wire.getState() == PinState::OFF) stateVal = 2.0f;

        // Register the START point of the wire
        auto startCoord = std::make_pair(path.front().x, path.front().y);
        endpointMap[startCoord].count++;
        endpointMap[startCoord].stateVal = stateVal;

        // Register the END point of the wire
        if (path.size() > 1) {
            auto endCoord = std::make_pair(path.back().x, path.back().y);
            endpointMap[endCoord].count++;
            endpointMap[endCoord].stateVal = stateVal;
        }
    }

    // A schematic intersection dot only appears if 3 or more wires connect at the same point!
    for (const auto& [coord, data] : endpointMap) {
        if (data.count >= 3) {
            intersections.push_back({ static_cast<float>(coord.first), static_cast<float>(coord.second), data.stateVal });
        }
    }

    return intersections;
}