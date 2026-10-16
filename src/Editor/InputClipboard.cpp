#include "Editor/Input.h"
#include "Editor/Scene.h"


void Input::beginPaste(GridCoords position)
{
    const auto result = m_paste.begin(*m_scene, m_clipboard, position);
    if (!result)
        reportEditResult(result);
    if (m_paste.active())
        clearHover();
}


/** @brief Commit the current paste preview and retain created identities as the new selection. */
void Input::placePaste(GridCoords position)
{
    m_paste.update(*m_scene, position);
    std::vector<Wire> paths;
    for (const auto& [id, wire] : m_paste.scene()->getVisibleWires())
        paths.push_back(wire);
    const auto result = m_paste.finish(*m_scene);
    recordEdit(result);
    if (result && result.change)
    {
        m_selection.selectGroup(result.createdComponentIds);
        m_selection.retainMovedWires(*m_scene, result.change->addedWireIds, paths);
    }
}
