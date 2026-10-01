#include "DragGesture.h"

#include "Editor/Actions/EditorActions.h"

bool DragGesture::begin(Scene& scene, int componentId)
{
    if (active())
        return false;
    m_preview = EditorActions(scene).beginMove(componentId);
    return active();
}

bool DragGesture::update(Scene& scene, GridCoords position)
{
    if (!m_preview)
        return false;
    if (EditorActions(scene).previewMove(*m_preview, position))
        return true;
    cancel(scene);
    return false;
}

EditResult DragGesture::finish(Scene& scene)
{
    if (!m_preview)
        return {};
    const auto handle = *m_preview;
    m_preview.reset();
    return EditorActions(scene).commitMove(handle);
}

void DragGesture::cancel(Scene& scene)
{
    if (m_preview)
        EditorActions(scene).cancelMove(*m_preview);
    m_preview.reset();
}
