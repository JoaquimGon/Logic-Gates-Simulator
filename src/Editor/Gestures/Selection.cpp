#include "Selection.h"

#include "Editor/Scene.h"
#include "Geometry/GridSystem.h"

#include <algorithm>

void Selection::clear()
{
    m_areaSelected = false;
    m_components.clear();
    m_wires.clear();
    m_segmentWire = INVALID_WIRE_ID;
    m_segment.reset();
    cancelBox();
}

bool Selection::selectComponent(int id)
{
    if (m_components.contains(id) && (group() || m_areaSelected))
        return true;
    const bool alreadySelected = m_components.contains(id);
    clear();
    if (!alreadySelected)
        m_components.insert(id);
    return !alreadySelected;
}

void Selection::selectPinOwner(int id)
{
    clear();
    m_components.insert(id);
}

void Selection::selectWire(const Scene& scene, WireId id, GridCoords position)
{
    clear();
    m_segmentWire = id;
    SelectedSegment segment;
    const Wire* wire = scene.getWire(id);
    if (wire && wire->getSegmentAt(position, segment.start, segment.end))
        m_segment = segment;
}

void Selection::toggleWire(WireId id)
{
    m_segmentWire = INVALID_WIRE_ID;
    m_segment.reset();
    if (!m_wires.erase(id))
        m_wires.insert(id);
}

void Selection::prune(const Scene& scene)
{
    std::erase_if(m_components, [&](int id) { return !scene.getCommittedComponentView(id); });
    std::erase_if(m_wires, [&](WireId id) { return !scene.getWire(id); });
    if (m_segmentWire != INVALID_WIRE_ID && !scene.getWire(m_segmentWire))
    {
        m_segmentWire = INVALID_WIRE_ID;
        m_segment.reset();
    }
}

void Selection::retainMovedWires(
    const Scene& scene, std::span<const WireId> added, std::span<const Wire> paths
)
{
    prune(scene);
    if (paths.empty())
        return;
    auto covered = [&](GridCoords a, GridCoords b)
    {
        const bool horizontal = a.y == b.y;
        const int low = horizontal ? std::min(a.x, b.x) : std::min(a.y, b.y);
        const int high = horizontal ? std::max(a.x, b.x) : std::max(a.y, b.y);
        std::vector<std::pair<int, int>> intervals;
        for (const auto& wire : paths)
        {
            const auto& points = wire.getPath();
            for (std::size_t i = 1; i < points.size(); ++i)
            {
                const auto c = points[i - 1], d = points[i];
                if (horizontal ? c.y != a.y || d.y != a.y : c.x != a.x || d.x != a.x)
                    continue;
                const int first = horizontal ? std::min(c.x, d.x) : std::min(c.y, d.y);
                const int last = horizontal ? std::max(c.x, d.x) : std::max(c.y, d.y);
                if (last >= low && first <= high)
                    intervals.emplace_back(std::max(low, first), std::min(high, last));
            }
        }
        std::sort(intervals.begin(), intervals.end());
        int cursor = low;
        for (const auto& [first, last] : intervals)
        {
            if (first > cursor)
                break;
            cursor = std::max(cursor, last);
            if (cursor >= high)
                return true;
        }
        return false;
    };
    for (WireId id : added)
    {
        const auto* wire = scene.getWire(id);
        if (!wire)
            continue;
        const auto& points = wire->getPath();
        bool selected = points.size() > 1;
        for (std::size_t i = 1; selected && i < points.size(); ++i)
            selected = covered(points[i - 1], points[i]);
        if (selected)
            m_wires.insert(id);
    }
}

void Selection::beginBox(glm::vec2 point)
{
    m_boxStart = point;
    m_boxEnd = point;
}

void Selection::updateBox(glm::vec2 point)
{
    if (boxing())
        m_boxEnd = point;
}

std::optional<BodyBounds> Selection::boxBounds() const
{
    if (!m_boxStart)
        return std::nullopt;
    return BodyBounds{
        std::min(m_boxStart->x, m_boxEnd.x),
        std::min(m_boxStart->y, m_boxEnd.y),
        std::max(m_boxStart->x, m_boxEnd.x),
        std::max(m_boxStart->y, m_boxEnd.y)
    };
}

void Selection::finishBox(const Scene& scene)
{
    const auto bounds = boxBounds();
    if (!bounds)
        return;
    clear();
    m_areaSelected = true;
    constexpr float tolerance = 1e-6f;
    auto inside = [&](float x, float y)
    {
        return x >= bounds->left - tolerance && x <= bounds->right + tolerance &&
               y >= bounds->bottom - tolerance && y <= bounds->top + tolerance;
    };
    for (const auto& [id, view] : scene.getComponentViewMap())
    {
        const auto body = view->getBodyBounds();
        if (inside(body.left, body.bottom) && inside(body.right, body.top))
            m_components.insert(id);
    }
    for (const auto& [id, wire] : scene.getWires())
        if (std::all_of(
                wire.getPath().begin(),
                wire.getPath().end(),
                [&](GridCoords point)
                {
                    const auto world = GridSystem::gridToWorld(point);
                    return inside(world.x, world.y);
                }
            ))
            m_wires.insert(id);
}

EditBatch
Selection::deletions(const HitResult& hover, std::optional<SelectedSegment> hoveredSegment) const
{
    EditBatch batch;
    for (int id : m_components)
        batch.push_back(DeleteComponent{id});
    for (WireId id : m_wires)
        batch.push_back(DeleteWire{id});
    if (!batch.empty())
        return batch;
    if (hover.type == HitType::COMPONENT_BODY)
        return {DeleteComponent{hover.componentId}};
    const auto id = m_segmentWire != INVALID_WIRE_ID ? m_segmentWire : hover.wireId;
    if (id == INVALID_WIRE_ID)
        return {};
    if (m_segmentWire == id && m_segment)
        return {DeleteWireSegment{id, m_segment->start, m_segment->end}};
    if (hover.wireId == id && hoveredSegment)
        return {DeleteWireSegment{id, hoveredSegment->start, hoveredSegment->end}};
    return {DeleteWire{id}};
}
