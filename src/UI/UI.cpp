#include "UI/UI.h"

#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Geometry/GridMetrics.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double rowStep = 42;
}

void UI::layout(const ComponentCatalog& catalog, CanvasSurface surface, Input& input)
{
    if (surface != m_surface || input.getMode() != EditorMode::Selection)
        cancel(input);
    m_surface = surface;
    m_canCreate = input.getMode() == EditorMode::Selection;
    const double width = std::max(0, surface.windowWidth);
    const double height = std::max(0, surface.windowHeight);
    m_panel = {0, 0, std::min(220.0, width * 0.45), height};
    input.setCanvasViewport(CanvasViewport{m_panel.width, 0, width - m_panel.width, height});
    m_list = {12, 76, std::max(0.0, m_panel.width - 24), std::max(0.0, height - 124)};
    m_maxScroll =
        std::max(0.0, static_cast<double>(catalog.definitions().size()) * rowStep - m_list.height);
    m_scroll = std::clamp(m_scroll, 0.0, m_maxScroll);
    m_buttons.clear();
    double y = m_list.y - m_scroll;
    for (const auto& [id, definition] : catalog.definitions())
    {
        m_buttons.push_back({id, definition.displayName, {m_list.x, y, m_list.width, 36}});
        y += rowStep;
    }
}

const UI::Button* UI::buttonAt(glm::dvec2 point) const
{
    for (const auto& button : m_buttons)
        if (button.bounds.y >= m_list.y &&
            button.bounds.y + button.bounds.height <= m_list.y + m_list.height &&
            button.bounds.contains(point.x, point.y))
            return &button;
    return nullptr;
}

std::optional<GridCoords> UI::dropPosition(const CanvasCameraFrame& camera) const
{
    if (!camera.valid() || !camera.viewport.contains(m_pointer.x, m_pointer.y))
        return std::nullopt;
    const auto world = camera.windowToWorld(m_pointer);
    if (!world)
        return std::nullopt;
    const double x = std::round(static_cast<double>(world->x) / GridMetrics::Spacing);
    const double y = std::round(static_cast<double>(world->y) / GridMetrics::Spacing);
    for (const double value : {x, y})
        if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max())
            return std::nullopt;
    return GridCoords{static_cast<int>(x), static_cast<int>(y)};
}

void UI::cancel(Input& input)
{
    if (dragging())
    {
        m_dragDefinition.clear();
        input.setUiCapture({});
    }
}

bool UI::handleInput(
    const UiInputEvent& event, Scene& scene, Input& input, const CanvasCameraFrame& camera
)
{
    if (event.kind == UiInputKind::WindowFocus)
    {
        if (!event.focused)
            cancel(input);
        return false;
    }
    if (event.kind == UiInputKind::Cursor || event.kind == UiInputKind::MouseButton)
        m_pointer = {event.x, event.y};
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_F2 && !dragging())
        input.setCanvasFocused(true);
    if (event.kind == UiInputKind::Key && dragging())
    {
        if (event.code == GLFW_KEY_ESCAPE && event.action == GLFW_PRESS)
            cancel(input);
        return true;
    }
    if (event.kind == UiInputKind::Text)
        return dragging();
    if (event.kind == UiInputKind::MouseButton)
    {
        if (dragging())
        {
            if (event.code == GLFW_MOUSE_BUTTON_RIGHT && event.action == GLFW_PRESS)
                cancel(input);
            else if (event.code == GLFW_MOUSE_BUTTON_LEFT && event.action == GLFW_RELEASE)
            {
                const auto id = m_dragDefinition;
                const auto position = dropPosition(camera);
                cancel(input);
                if (position && m_canCreate)
                {
                    const auto result =
                        EditorActions(scene).apply({CreateComponent{id, *position}});
                    m_message = result ? ""
                                       : (result.error == EditError::Overlap
                                              ? "Space occupied. Try elsewhere."
                                              : "Cannot place this component.");
                    input.setCanvasFocused(true);
                }
            }
            return true;
        }
        if (m_panel.contains(event.x, event.y))
        {
            if (m_canCreate && input.isIdle() && event.code == GLFW_MOUSE_BUTTON_LEFT &&
                event.action == GLFW_PRESS)
                if (const auto* button = buttonAt(m_pointer))
                {
                    m_dragDefinition = button->definitionId;
                    m_message.clear();
                    input.setUiCapture({true, true});
                }
            return true;
        }
    }
    if (event.kind == UiInputKind::Scroll && m_panel.contains(m_pointer.x, m_pointer.y))
    {
        if (!dragging() && std::isfinite(event.y))
        {
            m_scroll = std::clamp(m_scroll - event.y * rowStep, 0.0, m_maxScroll);
            layout(scene.getComponentCatalog(), m_surface, input);
        }
        return true;
    }
    return dragging() || (event.kind == UiInputKind::Cursor && m_panel.contains(event.x, event.y));
}
