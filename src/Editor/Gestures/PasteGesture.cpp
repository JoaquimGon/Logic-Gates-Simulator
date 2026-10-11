#include "Editor/Gestures/PasteGesture.h"

#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Geometry/GeometryQueries.h"
#include "Geometry/GridSystem.h"

#include <cstdint>
#include <limits>
#include <stdexcept>

PasteGesture::PasteGesture() = default;
PasteGesture::~PasteGesture() = default;

EditResult
PasteGesture::begin(const Scene& destination, const EditorClipboard& clipboard, GridCoords position)
{
    cancel();
    try
    {
        // Check destination definitions without changing either its catalog or its contents.
        if (clipboard.pasteBatch(destination, position).empty())
            return {};
        auto preview = std::make_unique<Scene>();
        auto batch = clipboard.pasteBatch(*preview, position);
        for (auto& operation : batch)
            if (auto* component = std::get_if<CreateComponent>(&operation))
                component->placement = PlacementPolicy::AllowOverlap;
        const auto created = EditorActions(*preview).apply(batch);
        if (!created)
            return {.error = created.error, .message = created.message};
        const auto wires = preview->getWireIds();
        const auto move = EditorActions(*preview).beginMove(created.createdComponentIds, wires);
        if (!move)
            return {
                .error = EditError::InvalidConfiguration,
                .message = "Cannot preview this selection."
            };
        m_copy = clipboard;
        m_geometry = preview->committedGeometry();
        m_scene = std::move(preview);
        m_move = move;
        m_origin = m_position = position;
        update(destination, position);
        return {};
    }
    catch (const std::invalid_argument& error)
    {
        cancel();
        return {.error = EditError::InvalidConfiguration, .message = error.what()};
    }
}

void PasteGesture::update(const Scene& destination, GridCoords position)
{
    if (!active() || (position == m_position && m_destinationRevision == destination.getRevision()))
        return;
    const auto x = std::int64_t(position.x) - m_origin.x;
    const auto y = std::int64_t(position.y) - m_origin.y;
    m_position = position;
    m_destinationRevision = destination.getRevision();
    m_blocked.clear();
    m_positionValid = x >= std::numeric_limits<int>::min() &&
                      x <= std::numeric_limits<int>::max() &&
                      y >= std::numeric_limits<int>::min() && y <= std::numeric_limits<int>::max();
    const GridCoords offset =
        m_positionValid ? GridCoords{static_cast<int>(x), static_cast<int>(y)} : GridCoords{};
    m_positionValid = m_positionValid && EditorActions(*m_scene).previewMoveBy(*m_move, offset);
    if (!m_positionValid)
    {
        for (const auto& component : m_geometry)
            m_blocked.insert(component.componentId);
        return;
    }
    auto geometry = destination.committedGeometry();
    const auto worldOffset = GridSystem::gridToWorld(offset);
    for (auto component : m_geometry)
    {
        // Preview IDs are temporary; negative IDs cannot collide with real scene IDs.
        component.componentId = -component.componentId - 1;
        component.origin.x += offset.x;
        component.origin.y += offset.y;
        component.centerX += worldOffset.x;
        component.centerY += worldOffset.y;
        for (auto& pin : component.pins)
        {
            pin.position.x += offset.x;
            pin.position.y += offset.y;
        }
        geometry.push_back(std::move(component));
    }
    for (const auto& component : m_geometry)
        if (overlapsComponent(geometry, -component.componentId - 1))
            m_blocked.insert(component.componentId);
}

EditResult PasteGesture::finish(Scene& destination)
{
    if (!active())
        return {};
    update(destination, m_position);
    if (!m_positionValid)
        return {
            .error = EditError::InvalidConfiguration,
            .message = "Paste exceeds the grid coordinate range."
        };
    if (!m_blocked.empty())
        return {
            .error = EditError::Overlap,
            .message = "Paste overlaps a component; move the preview to an empty area."
        };
    try
    {
        auto result = EditorActions(destination).apply(m_copy.pasteBatch(destination, m_position));
        if (result)
            cancel();
        return result;
    }
    catch (const std::invalid_argument& error)
    {
        return {.error = EditError::InvalidConfiguration, .message = error.what()};
    }
}

void PasteGesture::cancel()
{
    m_scene.reset();
    m_move.reset();
    m_geometry.clear();
    m_blocked.clear();
    m_destinationRevision.reset();
    m_positionValid = false;
    m_copy = {};
}
