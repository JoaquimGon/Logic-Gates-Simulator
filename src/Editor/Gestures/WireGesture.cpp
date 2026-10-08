#include "WireGesture.h"

#include "Editor/Scene.h"
#include "Geometry/WireRouting.h"

#include <cmath>

void WireGesture::begin(GridCoords position, PinRef origin, PinType direction)
{
    cancel();
    m_start = position;
    m_origin = origin;
    m_direction = direction;
    m_originComponent = origin.componentId;
    m_wire.setPath({position});
    m_active = true;
}

void WireGesture::beginBranch(WireId wireId, GridCoords position)
{
    cancel();
    m_start = position;
    m_branch = wireId;
}

void WireGesture::update(Scene& scene, GridCoords position)
{
    if (m_branch != INVALID_WIRE_ID)
    {
        if (position == m_start)
            return;
        const Wire* source = scene.getWire(m_branch);
        if (!source)
        {
            cancel();
            return;
        }
        const auto start = m_start;
        PinRef origin;
        PinType direction = PinType::OUTPUT;
        int originComponent = -1;
        PinState state = PinState::DISCONNECTED;
        if (const Net* net = scene.getNet(source->getNet()))
        {
            if (const auto driver = net->getDriver())
            {
                origin = *driver;
                originComponent = origin.componentId;
            }
            else if (!net->getSinks().empty())
                originComponent = net->getSinks().front().componentId;
            state = net->getState() == PinState::ON ? PinState::ON : PinState::DISCONNECTED;
        }
        begin(start, origin, direction);
        m_originComponent = originComponent;
        m_wire.setState(state);
    }
    if (!m_active)
        return;
    if (position != m_start && !m_axisLocked)
    {
        m_xFirst = std::abs(position.x - m_start.x) >= std::abs(position.y - m_start.y);
        m_axisLocked = true;
    }
    else if (position == m_start)
        m_axisLocked = false;
    auto allowedNet = scene.netOfPin(m_origin, m_direction);
    if (allowedNet == INVALID_NET_ID)
        for (const auto& [id, wire] : scene.getWires())
            if (wire.containsPoint(m_start))
            {
                allowedNet = wire.getNet();
                break;
            }
    const auto path = routeWire(
        m_start, position, scene.committedGeometry(), scene.getWires(), allowedNet, m_xFirst
    );
    m_routeBlocked = !path;
    m_wire.setPath(path.value_or(std::vector<GridCoords>{m_start}));
}

std::optional<AddWire> WireGesture::finish(const HitResult& hit)
{
    std::optional<AddWire> result;
    if (m_active && m_wire.getPath().size() > 1 &&
        !(hit.type == HitType::COMPONENT_PIN && m_originComponent != -1 &&
          hit.componentId == m_originComponent))
        result = AddWire{m_wire.getPath()};
    cancel();
    return result;
}

void WireGesture::cancel()
{
    m_wire = Wire{};
    m_origin = {};
    m_direction = PinType::INPUT;
    m_originComponent = -1;
    m_branch = INVALID_WIRE_ID;
    m_routeBlocked = false;
    m_active = m_axisLocked = false;
    m_xFirst = true;
    m_start = {};
}
