#include "DragGesture.h"

#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"

#include <limits>

bool DragGesture::begin(Scene& scene, int componentId)
{
    const auto* view = scene.getCommittedComponentView(componentId);
    return view && begin(scene, {&componentId, 1}, {}, view->getGridPosition());
}

bool DragGesture::begin(
    Scene& scene, std::span<const int> components, std::span<const WireId> wires, GridCoords pointer
)
{
    if (active())
        return false;
    m_start = pointer;
    m_preview = EditorActions(scene).beginMove(components, wires);
    return active();
}

bool DragGesture::update(Scene& scene, GridCoords position)
{
    if (!m_preview)
        return false;
    const auto x = std::int64_t(position.x) - m_start.x, y = std::int64_t(position.y) - m_start.y;
    if (x >= std::numeric_limits<int>::min() && x <= std::numeric_limits<int>::max() &&
        y >= std::numeric_limits<int>::min() && y <= std::numeric_limits<int>::max() &&
        EditorActions(scene).previewMoveBy(*m_preview, {static_cast<int>(x), static_cast<int>(y)}))
        return true;
    cancel(scene);
    return false;
}

EditResult DragGesture::finish(Scene& scene, bool reroute)
{
    if (!m_preview)
        return {};
    const auto handle = *m_preview;
    m_preview.reset();
    return EditorActions(scene).commitMove(handle, reroute);
}

void DragGesture::cancel(Scene& scene)
{
    if (m_preview)
        EditorActions(scene).cancelMove(*m_preview);
    m_preview.reset();
}
