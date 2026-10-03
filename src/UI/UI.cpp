#include "UI/UI.h"

#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "Geometry/GridMetrics.h"
#include "Geometry/GridSystem.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
constexpr double cardHeight = 88, cardStep = 96, rowHeight = 48, rowStep = 56;
}

void UI::layout(const ComponentCatalog& catalog, CanvasSurface surface, Input& input)
{
    if (surface != m_surface)
        closeInfo();
    if (surface != m_surface || input.getMode() != EditorMode::Selection)
        cancel(input);
    m_surface = surface;
    m_canCreate = input.getMode() == EditorMode::Selection;
    const double width = std::max(0, surface.windowWidth);
    const double height = std::max(0, surface.windowHeight);
    m_panel = {0, 0, std::min(220.0, width * 0.45), height};
    input.setCanvasViewport(CanvasViewport{m_panel.width, 0, width - m_panel.width, height});
    m_list = {12, 112, std::max(0.0, m_panel.width - 24), std::max(0.0, height - 160)};
    m_buttons.clear();
    auto add = [&](const ComponentDefinition& definition)
    {
        Button button{definition.identity.id, definition.displayName, {}};
        for (const auto& pin : definition.layout.pins)
            (pin.direction == PinType::INPUT ? button.inputCount : button.outputCount)++;
        m_buttons.push_back(std::move(button));
    };
    if (m_tab == Tab::Native)
    {
        for (const auto& native : nativeDefinitions())
            if (const auto* definition = catalog.find(native.identity.id))
                add(*definition);
    }
    else
        for (const auto& [id, definition] : catalog.definitions())
            if (!id.starts_with("native."))
                add(definition);

    const int columns = m_tab == Tab::Native && m_list.width >= 160 ? 2 : 1;
    const double step = m_tab == Tab::Native ? cardStep : rowStep;
    const double itemHeight = m_tab == Tab::Native ? cardHeight : rowHeight;
    const auto rows = (m_buttons.size() + columns - 1) / columns;
    const double contentHeight = rows == 0 ? 0 : (rows - 1) * step + itemHeight;
    m_maxScroll = std::max(0.0, contentHeight - m_list.height);
    m_scroll = std::clamp(m_scroll, 0.0, m_maxScroll);
    const double itemWidth = (m_list.width - (columns - 1) * 8) / columns;
    for (std::size_t i = 0; i < m_buttons.size(); ++i)
        m_buttons[i].bounds = {
            m_list.x + (i % columns) * (itemWidth + 8),
            m_list.y + (i / columns) * step - m_scroll,
            itemWidth,
            itemHeight
        };
}

CanvasViewport UI::tabBounds(Tab tab) const
{
    const double width = std::max(0.0, (m_panel.width - 32) / 2);
    return {12 + (tab == Tab::Custom ? width + 8 : 0), 70, width, 28};
}

const UI::Button* UI::buttonAt(glm::dvec2 point) const
{
    if (!m_list.contains(point.x, point.y))
        return nullptr;
    for (const auto& button : m_buttons)
        if (button.bounds.contains(point.x, point.y))
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

void UI::closeInfo()
{
    m_infoComponent = -1;
    m_infoScroll = 0;
}

std::vector<std::string> UI::componentInfo(const Scene& scene) const
{
    const auto* view = scene.getCommittedComponentView(m_infoComponent);
    if (!view)
        return {};
    const auto* definition = scene.getComponentCatalog().find(view->getDefinitionIdentity().id);
    std::vector<std::string> lines{definition ? definition->displayName : "Component"};
    if (!view->getBodyLabel().empty() && view->getBodyLabel() != lines.front())
        lines.push_back("Label: " + view->getBodyLabel());
    auto pins = [&](const std::vector<PinUI>& values, const char* heading, const char* fallback)
    {
        lines.push_back(std::string(heading) + (values.empty() ? ": none" : ""));
        for (const auto& pin : values)
        {
            const auto name = pin.label.empty()
                                  ? std::string(fallback) + " " + std::to_string(pin.pin_index + 1)
                                  : pin.label;
            const char* state = pin.state == PinState::ON    ? "1 (ON)"
                                : pin.state == PinState::OFF ? "0 (OFF)"
                                                             : "Unavailable";
            lines.push_back(name + ": " + state);
        }
    };
    pins(view->getInputPins(), "Inputs", "Input");
    pins(view->getOutputPins(), "Outputs", "Output");
    return lines;
}

CanvasViewport UI::infoBounds(const Scene& scene) const
{
    const auto lines = componentInfo(scene);
    if (lines.empty())
        return {};
    const double width = std::min(280.0, std::max(0.0, m_surface.windowWidth - 16.0));
    const double height = std::min(
        infoHeaderHeight + infoFooterHeight + static_cast<double>(lines.size() - 1) * infoRowHeight,
        std::max(0.0, m_surface.windowHeight - 16.0)
    );
    return {
        std::clamp(m_infoAnchor.x + 14, 8.0, std::max(8.0, m_surface.windowWidth - width - 8)),
        std::clamp(m_infoAnchor.y + 14, 8.0, std::max(8.0, m_surface.windowHeight - height - 8)),
        width,
        height
    };
}

int UI::infoVisibleRows(const Scene& scene) const
{
    return std::max(
        0,
        static_cast<int>(
            (infoBounds(scene).height - infoHeaderHeight - infoFooterHeight) / infoRowHeight
        )
    );
}

bool UI::handleInput(
    const UiInputEvent& event, Scene& scene, Input& input, const CanvasCameraFrame& camera
)
{
    if (m_infoComponent != -1 && !scene.getCommittedComponentView(m_infoComponent))
        closeInfo();
    if (event.kind == UiInputKind::WindowFocus)
    {
        if (!event.focused)
        {
            cancel(input);
            closeInfo();
            m_infoRightPressed = m_infoEscapePressed = false;
        }
        return false;
    }
    if (event.kind == UiInputKind::Cursor || event.kind == UiInputKind::MouseButton)
        m_pointer = {event.x, event.y};
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_F2 && !dragging())
        input.setCanvasFocused(true);
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_ESCAPE &&
        (m_infoComponent != -1 || m_infoEscapePressed))
    {
        closeInfo();
        m_infoEscapePressed = event.action != GLFW_RELEASE;
        if (!m_infoEscapePressed)
            input.setCanvasFocused(true);
        return true;
    }
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
        if (event.code == GLFW_MOUSE_BUTTON_RIGHT && event.action == GLFW_RELEASE &&
            m_infoRightPressed)
        {
            m_infoRightPressed = false;
            input.setCanvasFocused(true);
            return true;
        }
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
        if (m_infoComponent != -1)
        {
            if (infoBounds(scene).contains(event.x, event.y))
                return true;
            if (event.action == GLFW_PRESS)
            {
                closeInfo();
                if (event.code == GLFW_MOUSE_BUTTON_LEFT)
                    return true;
            }
        }
        if (m_panel.contains(event.x, event.y))
        {
            if (event.code == GLFW_MOUSE_BUTTON_LEFT && event.action == GLFW_PRESS)
                for (const auto tab : {Tab::Native, Tab::Custom})
                    if (tabBounds(tab).contains(event.x, event.y))
                    {
                        input.cancelCurrentAction();
                        m_tab = tab;
                        m_scroll = 0;
                        m_message.clear();
                        layout(scene.getComponentCatalog(), m_surface, input);
                        return true;
                    }
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
        if (event.code == GLFW_MOUSE_BUTTON_RIGHT && event.action == GLFW_PRESS && input.isIdle() &&
            camera.valid() && camera.viewport.contains(event.x, event.y))
            if (const auto world = camera.windowToWorld(m_pointer))
            {
                const auto hit = scene.hitTest(*world, GridSystem::worldToGrid(*world));
                if (hit.type == HitType::COMPONENT_BODY || hit.type == HitType::COMPONENT_PIN)
                {
                    m_infoComponent = hit.componentId;
                    m_infoAnchor = m_pointer;
                    m_infoScroll = 0;
                    m_infoRightPressed = true;
                    return true;
                }
            }
    }
    if (m_infoComponent != -1 && infoBounds(scene).contains(m_pointer.x, m_pointer.y))
    {
        if (event.kind == UiInputKind::Scroll && std::isfinite(event.y))
        {
            const int maximum = std::max(
                0, static_cast<int>(componentInfo(scene).size()) - 1 - infoVisibleRows(scene)
            );
            m_infoScroll = static_cast<int>(
                std::clamp(m_infoScroll - event.y, 0.0, static_cast<double>(maximum))
            );
            return true;
        }
        if (event.kind == UiInputKind::Cursor)
            return true;
    }
    if (event.kind == UiInputKind::Scroll && m_panel.contains(m_pointer.x, m_pointer.y))
    {
        if (!dragging() && std::isfinite(event.y))
        {
            const double step = m_tab == Tab::Native ? cardStep : rowStep;
            m_scroll = std::clamp(m_scroll - event.y * step, 0.0, m_maxScroll);
            layout(scene.getComponentCatalog(), m_surface, input);
        }
        return true;
    }
    return dragging() || (event.kind == UiInputKind::Cursor && m_panel.contains(event.x, event.y));
}
