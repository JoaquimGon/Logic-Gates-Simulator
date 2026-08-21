#include "..\..\include\Engine\Scene.h"
#include <cmath>

int Scene::addGate(GateType type, GridCoords gridPos, glm::vec2 size, const std::string& shaderName,
    std::vector<PinUI> inputs, std::vector<PinUI> outputs, bool outInverted)
{
    int id = m_circuit.addGate(type, outInverted);
    m_gateViews.emplace(id, GateView(gridPos, id, size, shaderName, std::move(inputs), std::move(outputs)));
    return id;
}

void Scene::removeGate(int gateId)
{
    m_circuit.delGate(gateId);
    m_gateViews.erase(gateId);

    // Don't leave wires pointing at a gate id that no longer exists
    for (auto& wire : m_wires) {
        if (wire.hasSource() && wire.getSource().gateId == gateId) wire.disconnectSource();
        if (wire.hasDest() && wire.getDest().gateId == gateId) wire.disconnectDest();
    }
}

GateView* Scene::getGateView(int gateId)
{
    auto it = m_gateViews.find(gateId);
    return it != m_gateViews.end() ? &it->second : nullptr;
}

Gate* Scene::getLogicGate(int gateId)
{
    return m_circuit.getGate(gateId);
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

bool Scene::connectPins(int srcGateId, int destGateId, int destPinIndex)
{
    return m_circuit.connectGates(srcGateId, destGateId, destPinIndex);
}

void Scene::disconnectPins(int srcGateId, int destGateId, int destPinIndex)
{
    m_circuit.disconnectGates(srcGateId, destGateId, destPinIndex);
}

void Scene::reconnectWiresToGate(int gateId)
{
    GateView* gate = getGateView(gateId);
    if (!gate) return;

    // Input pins: look for a wire needing a destination
    for (const auto& pin : gate->m_inputs) {
        GridCoords pinPos = gate->getAbsolutePinGridPos(pin);
        bool handled = false;

        for (size_t i = 0; i < m_wires.size() && !handled; ++i) {
            Wire& wire = m_wires[i];
            if (wire.getPath().empty()) continue;

            if (!wire.hasDest() && (wire.getPath().front() == pinPos || wire.getPath().back() == pinPos)) {
                wire.setDest(gateId, static_cast<int>(pin.pin_index));
                if (wire.hasSource()) connectPins(wire.getSource().gateId, gateId, static_cast<int>(pin.pin_index));
                handled = true;
            }
            else if (wire.containsPoint(pinPos)) {
                Wire wireA, wireB;
                if (splitWireAt(i, pinPos, wireA, wireB)) {
                    wireA.setDest(gateId, static_cast<int>(pin.pin_index));
                    if (wireA.hasSource()) connectPins(wireA.getSource().gateId, gateId, static_cast<int>(pin.pin_index));
                    addWires(wireA, wireB);
                }
                handled = true;
            }
        }
    }

    // Output pin(s): look for a wire needing a source
    for (const auto& pin : gate->m_outputs) {
        GridCoords pinPos = gate->getAbsolutePinGridPos(pin);
        bool handled = false;

        for (size_t i = 0; i < m_wires.size() && !handled; ++i) {
            Wire& wire = m_wires[i];
            if (wire.getPath().empty()) continue;

            if (!wire.hasSource() && (wire.getPath().front() == pinPos || wire.getPath().back() == pinPos)) {
                wire.setSource(gateId, static_cast<int>(pin.pin_index));
                if (wire.hasDest()) connectPins(gateId, wire.getDest().gateId, wire.getDest().pinIndex);
                handled = true;
            }
            else if (wire.containsPoint(pinPos)) {
                Wire wireA, wireB;
                if (splitWireAt(i, pinPos, wireA, wireB)) {
                    wireB.setSource(gateId, static_cast<int>(pin.pin_index));
                    if (wireB.hasDest()) connectPins(gateId, wireB.getDest().gateId, wireB.getDest().pinIndex);
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
    for (const auto& [id, gate] : m_gateViews) {
        for (const auto& pin : gate.m_inputs)
            if (gridPos == gate.getAbsolutePinGridPos(pin))
                return { HitType::GATE_PIN, id, static_cast<int>(pin.pin_index), PinType::INPUT, -1 };

        for (const auto& pin : gate.m_outputs)
            if (gridPos == gate.getAbsolutePinGridPos(pin))
                return { HitType::GATE_PIN, id, static_cast<int>(pin.pin_index), PinType::OUTPUT, -1 };
    }

    // 2. Wire endpoints / bodies
    for (size_t i = 0; i < m_wires.size(); ++i) {
        const auto& path = m_wires[i].getPath();
        if (path.empty()) continue;

        if (gridPos == path.back())  return { HitType::WIRE_END,   -1, -1, PinType::INPUT, static_cast<int>(i) };
        if (gridPos == path.front()) return { HitType::WIRE_START, -1, -1, PinType::INPUT, static_cast<int>(i) };
        if (m_wires[i].containsPoint(gridPos)) return { HitType::WIRE_BODY, -1, -1, PinType::INPUT, static_cast<int>(i) };
    }

    // 3. Gate bodies — world-space AABB, since footprints don't align perfectly to the grid
    for (const auto& [id, gate] : m_gateViews) {
        glm::vec2 halfSize = gate.getSize() * 0.5f;
        glm::vec2 delta = worldPos - gate.getPosition();
        if (std::abs(delta.x) <= halfSize.x && std::abs(delta.y) <= halfSize.y)
            return { HitType::GATE_BODY, id, -1, PinType::INPUT, -1 };
    }

    return {};
}

void Scene::propagate()
{
    m_circuit.propagate();
}

void Scene::syncVisuals()
{
    for (auto& [id, gateView] : m_gateViews) {
        Gate* logicGate = m_circuit.getGate(id);
        if (!logicGate) continue;

        gateView.getOutputPinUI().state = logicGate->getStateOutPin() ? PinState::ON : PinState::OFF;

        auto inSignals = logicGate->getStateInPins();
        for (size_t i = 0; i < inSignals.size(); ++i)
            gateView.getInputPinUI(i).state = inSignals[i] ? PinState::ON : PinState::OFF;
    }

    for (auto& wire : m_wires) {
        if (wire.hasSource()) {
            if (Gate* srcGate = m_circuit.getGate(wire.getSource().gateId))
                wire.setState(srcGate->getStateOutPin() ? PinState::ON : PinState::OFF);
        }
        else {
            wire.setState(PinState::DISCONNECTED);
        }
    }
}