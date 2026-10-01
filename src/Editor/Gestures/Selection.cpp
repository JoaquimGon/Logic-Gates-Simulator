#include "Selection.h"

#include "Editor/Scene.h"

void Selection::clear()
{
    m_component = -1;
    m_wire = INVALID_WIRE_ID;
    m_segment.reset();
}

bool Selection::selectComponent(int id)
{
    const bool alreadySelected = m_component == id;
    clear();
    if (!alreadySelected)
        m_component = id;
    return !alreadySelected;
}

void Selection::selectPinOwner(int id)
{
    clear();
    m_component = id;
}

void Selection::selectWire(const Scene& scene, WireId id, GridCoords position)
{
    clear();
    m_wire = id;
    SelectedSegment segment;
    const Wire* wire = scene.getWire(id);
    if (wire && wire->getSegmentAt(position, segment.start, segment.end))
        m_segment = segment;
}

std::optional<EditOperation>
Selection::deletion(const HitResult& hover, std::optional<SelectedSegment> hoveredSegment) const
{
    const int component = m_component != -1
                              ? m_component
                              : (hover.type == HitType::COMPONENT_BODY ? hover.componentId : -1);
    if (component != -1)
        return DeleteComponent{component};
    const auto id = m_wire != INVALID_WIRE_ID ? m_wire : hover.wireId;
    if (id == INVALID_WIRE_ID)
        return std::nullopt;
    if (m_wire == id && m_segment)
        return DeleteWireSegment{id, m_segment->start, m_segment->end};
    if (hover.wireId == id && hoveredSegment)
        return DeleteWireSegment{id, hoveredSegment->start, hoveredSegment->end};
    return DeleteWire{id};
}
