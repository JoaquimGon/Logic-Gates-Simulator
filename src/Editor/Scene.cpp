#include "Scene.h"

#include "Geometry/GeometryQueries.h"
#include "Geometry/WireNormalization.h"

#include <stdexcept>
#include <utility>

namespace
{
/** @brief Checks that visual pins map bijectively to the component's logical pins. */
void validatePins(const std::vector<PinUI>& pins, PinType type, int pinCount)
{
    if (pins.size() != static_cast<size_t>(pinCount))
        throw std::invalid_argument("View and component pin counts must match.");

    std::vector<bool> seen(pinCount, false);
    for (const auto& pin : pins)
    {
        if (pin.type != type || pin.pin_index >= static_cast<size_t>(pinCount) ||
            seen[pin.pin_index])
            throw std::invalid_argument("View pin indices must be unique, valid, and directional.");
        seen[pin.pin_index] = true;
    }
}

} // namespace

ComponentView* Scene::getComponentView(int componentId)
{
    const auto& views = getComponentViewMap();
    auto it = views.find(componentId);
    return it != views.end() ? it->second.get() : nullptr;
}

Component* Scene::getLogicComponent(int componentId)
{
    return m_circuit.getComponent(componentId);
}

const Component* Scene::getLogicComponent(int componentId) const
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

void Scene::rebuildNets()
{
    for (const auto& [id, view] : m_componentViews)
    {
        Component* component = m_circuit.getComponent(id);
        if (!component)
            throw std::logic_error("Component view has no logical component.");
        validatePins(view->getInputPins(), PinType::INPUT, component->getInputPinCount());
        validatePins(view->getOutputPins(), PinType::OUTPUT, component->getOutputPinCount());
    }

    m_previewViews.clear();
    m_previewToken = 0;
    m_previewComponentId = -1;
    m_previewComponentIds.clear();
    m_previewWireIds.clear();
    m_previewWires.clear();
    m_previewOffset = {};
    ++m_revision;
    ++m_topologyBuildCount;

    const auto geometry = committedGeometry();
    std::vector<PinAnchor> pins;
    for (const auto& component : geometry)
        pins.insert(pins.end(), component.pins.begin(), component.pins.end());
    normalizeWires(m_wires, pins, m_nextWireId);
    auto topology = buildConnectivity(m_wires, pins, m_circuit, m_nextNetId);
    m_nets = std::move(topology.nets);
    m_pinNet = std::move(topology.pinNets);
    m_nextNetId = topology.nextNetId;
    m_rejectedConnections = std::move(topology.rejectedConnections);
    m_topologyResult = topology.status;
    m_topologyInvalidNeedsUpdate = m_topologyResult != SimulationResult::OK;
    for (auto& [id, wire] : m_wires)
        wire.setNet(topology.wireNets.at(id));
    if (m_topologyInvalidNeedsUpdate)
        syncVisuals();
}

std::vector<ComponentGeometry> Scene::committedGeometry() const
{
    std::vector<ComponentGeometry> geometry;
    geometry.reserve(m_componentViews.size());
    for (const auto& [id, view] : m_componentViews)
    {
        const auto bounds = view->getBodyBounds();
        ComponentGeometry component{
            id,
            view->getGridPosition(),
            bounds.centerX(),
            bounds.centerY(),
            bounds.width(),
            bounds.height(),
            {}
        };
        for (const auto* pins : {&view->getInputPins(), &view->getOutputPins()})
            for (const auto& pin : *pins)
                component.pins.push_back(
                    {{id, static_cast<int>(pin.pin_index)},
                     pin.type,
                     view->getAbsolutePinGridPos(pin)}
                );
        geometry.push_back(std::move(component));
    }
    return geometry;
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
    if (!pin.isConnected() || getLastEvalResult() != SimulationResult::OK)
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
    return hitGeometry(committedGeometry(), m_wires, worldPos.x, worldPos.y, gridPos);
}

bool Scene::getCollinearOverlap(
    GridCoords a, GridCoords b, GridCoords c, GridCoords d, GridCoords& outStart, GridCoords& outEnd
) const
{
    return collinearOverlap(a, b, c, d, outStart, outEnd);
}

bool Scene::updateClocks(float deltaTime)
{
    return m_topologyResult == SimulationResult::OK && m_circuit.updateClocks(deltaTime);
}

SimulationResult Scene::propagate()
{
    if (m_topologyResult != SimulationResult::OK)
    {
        m_topologyInvalidNeedsUpdate = false;
        return m_topologyResult;
    }
    return m_circuit.propagate();
}

void Scene::togglePauseAllClocks()
{
    std::vector<Clock*> clocks;
    m_circuit.collectClocks(clocks);
    for (auto* clock : clocks)
        clock->togglePause();
}

void Scene::stepAllClocks()
{
    std::vector<Clock*> clocks;
    m_circuit.collectClocks(clocks);
    bool stepped = false;
    for (auto* clock : clocks)
        stepped |= clock->step();
    if (stepped)
        markSimulationDirty();
}

void Scene::setAllClocksFrequency(float hz)
{
    validateClockFrequency(hz);
    std::vector<Clock*> clocks;
    m_circuit.collectClocks(clocks);
    for (auto* clock : clocks)
        clock->setFrequency(hz);
}

void Scene::syncVisuals()
{
    const bool blocked = getLastEvalResult() != SimulationResult::OK;
    auto syncViews = [&](auto& views)
    {
        for (auto& [id, view] : views)
        {
            Component* comp = m_circuit.getComponent(id);
            if (!comp)
                continue;

            for (auto& pin : view->editOutputPins())
                pin.state = blocked ? PinState::DISCONNECTED
                                    : (comp->getStateOutPin(static_cast<int>(pin.pin_index))
                                           ? PinState::ON
                                           : PinState::OFF);

            for (auto& pin : view->editInputPins())
                pin.state = blocked ? PinState::DISCONNECTED
                                    : (comp->getStateInPin(static_cast<int>(pin.pin_index))
                                           ? PinState::ON
                                           : PinState::OFF);
        }
    };
    syncViews(m_componentViews);
    syncViews(m_previewViews);

    // Refresh Net states from single driver
    for (auto& [netId, net] : m_nets)
    {
        if (blocked || net.shorted())
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
    for (auto& [id, wire] : m_previewWires)
        if (const auto* committed = getWire(id))
            wire.setState(committed->getState());
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
    std::vector<PinAnchor> pins;
    for (const auto& [id, view] : getComponentViewMap())
        for (const auto* group : {&view->getInputPins(), &view->getOutputPins()})
            for (const auto& pin : *group)
                pins.push_back(
                    {{id, static_cast<int>(pin.pin_index)},
                     pin.type,
                     view->getAbsolutePinGridPos(pin)}
                );
    std::vector<glm::vec3> intersections;
    for (const auto& junction : wireJunctions(getVisibleWires(), pins))
    {
        const float state = junction.state == PinState::ON    ? 1.0f
                            : junction.state == PinState::OFF ? 2.0f
                                                              : 0.0f;
        intersections.push_back(
            {static_cast<float>(junction.position.x),
             static_cast<float>(junction.position.y),
             state}
        );
    }
    return intersections;
}

bool Scene::checkOverlap(int draggedComponentId) const
{
    return overlapsComponent(committedGeometry(), draggedComponentId);
}
