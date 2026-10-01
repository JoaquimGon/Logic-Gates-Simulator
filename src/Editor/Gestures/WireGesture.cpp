#include "WireGesture.h"

#include "Editor/Scene.h"

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
    std::vector<GridCoords> path{m_start};
    if (position != m_start)
    {
        if (position.x != m_start.x && position.y != m_start.y)
            path.push_back(
                m_xFirst ? GridCoords{position.x, m_start.y} : GridCoords{m_start.x, position.y}
            );
        path.push_back(position);
    }
    m_wire.setPath(path);
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
    m_active = m_axisLocked = false;
    m_xFirst = true;
    m_start = {};
}
