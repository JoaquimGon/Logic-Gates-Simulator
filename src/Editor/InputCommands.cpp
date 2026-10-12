#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Geometry/GridSystem.h"
#include "Simulation/ClockTimeline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

void Input::resetSimulation()
{
    if (!m_scene)
        return;
    cancelCurrentAction();
    m_scene->resetSimulation();
    if (m_clockTimeline)
        m_clockTimeline->clear();
}

bool Input::canEditCommand(EditCommand command) const
{
    if (!m_scene || m_mode != EditorMode::Selection || !isIdle())
        return false;
    const bool selected = !m_selection.components().empty() || !m_selection.wires().empty() ||
                          m_selection.segment().has_value();
    switch (command)
    {
    case EditCommand::Undo:
        return getUndoCount() > 0;
    case EditCommand::Redo:
        return getRedoCount() > 0;
    case EditCommand::Copy:
    case EditCommand::Delete:
        return selected;
    case EditCommand::Paste:
        return !m_clipboard.empty();
    case EditCommand::SelectAll:
        return m_scene->getComponentCount() > 0 || m_scene->wireCount() > 0;
    }
    return false;
}

bool Input::deleteObjects(const HitResult& hit, std::optional<SelectedSegment> segment)
{
    const auto batch = m_selection.deletions(hit, segment);
    if (batch.empty())
        return false;
    const auto result = EditorActions(*m_scene).apply(batch);
    recordEdit(result);
    if (result)
        m_selection.clear();
    return static_cast<bool>(result);
}

bool Input::executeEditCommand(EditCommand command, GridCoords pastePosition, bool waitForCanvas)
{
    if (!m_scene || m_mode != EditorMode::Selection)
        return false;
    const bool history = command == EditCommand::Undo || command == EditCommand::Redo;
    if (history && !isIdle())
    {
        cancelCurrentAction();
        m_pendingKeyPresses.clear();
        return true;
    }
    if (!canEditCommand(command))
        return false;
    switch (command)
    {
    case EditCommand::Undo:
    case EditCommand::Redo:
    {
        cancelCurrentAction();
        m_pendingKeyPresses.clear();
        const auto result =
            command == EditCommand::Redo ? m_history->redo(*m_scene) : m_history->undo(*m_scene);
        reportEditResult(result);
        if (result.change && m_clockTimeline)
            m_clockTimeline->clear();
        return static_cast<bool>(result);
    }
    case EditCommand::Copy:
        return m_clipboard.copy(*m_scene, m_selection);
    case EditCommand::Paste:
        beginPaste(pastePosition);
        m_pasteAwaitingCanvas = waitForCanvas && m_paste.active();
        return m_paste.active();
    case EditCommand::Delete:
        return deleteObjects({}, {});
    case EditCommand::SelectAll:
    {
        std::vector<int> components;
        for (const auto& [id, view] : m_scene->getComponentViewMap())
            components.push_back(id);
        m_selection.selectGroup(components);
        for (const auto& [id, wire] : m_scene->getWires())
            m_selection.toggleWire(id);
        clearHover();
        return true;
    }
    }
    return false;
}

bool Input::canViewCommand(ViewCommand command) const
{
    if (command == ViewCommand::ResetZoom)
        return true;
    if (!m_scene)
        return false;
    if (command == ViewCommand::FitCircuit)
        return m_scene->getComponentCount() > 0 || m_scene->wireCount() > 0;
    return !m_selection.components().empty() || !m_selection.wires().empty() ||
           m_selection.segment().has_value();
}

bool Input::executeViewCommand(ViewCommand command, CanvasSurface surface)
{
    if (!canViewCommand(command))
        return false;
    if (command == ViewCommand::ResetZoom)
    {
        setZoom(1);
        return true;
    }
    float left = std::numeric_limits<float>::infinity(), bottom = left;
    float right = -left, top = -left;
    auto include = [&](float x, float y)
    {
        left = std::min(left, x);
        right = std::max(right, x);
        bottom = std::min(bottom, y);
        top = std::max(top, y);
    };
    const bool all = command == ViewCommand::FitCircuit;
    for (const auto& [id, view] : m_scene->getComponentViewMap())
        if (all || m_selection.containsComponent(id))
        {
            const auto bounds = view->getBodyBounds();
            include(bounds.left, bounds.bottom);
            include(bounds.right, bounds.top);
        }
    for (const auto& [id, wire] : m_scene->getWires())
        if (all || m_selection.containsWire(id))
            for (const auto point : wire.getPath())
            {
                const auto world = GridSystem::gridToWorld(point);
                include(world.x, world.y);
            }
    if (!all && m_selection.segment())
        for (const auto point : {m_selection.segment()->start, m_selection.segment()->end})
        {
            const auto world = GridSystem::gridToWorld(point);
            include(world.x, world.y);
        }
    if (!std::isfinite(left))
        return false;
    if (all)
    {
        const auto frame = m_camera.frame(surface);
        if (!frame.valid())
            return false;
        const float aspect =
            static_cast<float>(frame.framebufferViewport.width) / frame.framebufferViewport.height;
        const float zoom =
            0.85f *
            std::min(2 * aspect / std::max(0.1f, right - left), 2 / std::max(0.1f, top - bottom));
        setZoom(std::clamp(zoom, 0.000001f, 5.0f));
    }
    setPanOffset({left + (right - left) / 2, bottom + (top - bottom) / 2});
    return true;
}
