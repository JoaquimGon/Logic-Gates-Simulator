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
    {
        closeFileMenu(input);
        closeInfo(input);
        m_circuitTabs.cancel(input);
    }
    if (surface != m_surface || input.getMode() != EditorMode::Selection)
        cancel(input);
    m_surface = surface;
    m_canCreate = input.getMode() == EditorMode::Selection;
    const double width = std::max(0, surface.windowWidth);
    const double height = std::max(0, surface.windowHeight);
    m_bar = {0, 0, width, std::min(30.0, height)};
    m_panel = {0, m_bar.height, std::min(220.0, width * 0.45), height - m_bar.height};
    const double tabs = m_circuitTabs.enabled() ? std::min(30.0, m_panel.height) : 0;
    const double top = m_bar.height + tabs;
    m_circuitTabs.layout({m_panel.width, m_bar.height, width - m_panel.width, tabs});
    const double bottomHeight = std::min(180.0, (height - top) * 0.4);
    m_bottom = {m_panel.width, height - bottomHeight, width - m_panel.width, bottomHeight};
    input.setCanvasViewport(
        CanvasViewport{m_panel.width, top, width - m_panel.width, height - top - bottomHeight}
    );
    m_list = {
        12, m_panel.y + 112, std::max(0.0, m_panel.width - 24), std::max(0.0, m_panel.height - 160)
    };
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

void UI::setCircuitViews(CircuitViews* views, const FontMetrics* font)
{
    m_circuitTabs.bind(views, font);
}

void UI::update(double now, Input& input)
{
    if (m_fileMenuOpen)
        return;
    m_circuitTabs.update(now, input);
    if (m_circuitTabs.popupIndex())
        closeInfo(input);
}

CanvasViewport UI::tabBounds(Tab tab) const
{
    const double width = std::max(0.0, (m_panel.width - 32) / 2);
    return {12 + (tab == Tab::Custom ? width + 8 : 0), m_panel.y + 70, width, 28};
}

CanvasViewport UI::bottomTabBounds(BottomTab tab) const
{
    const double width = std::min(148.0, std::max(0.0, (m_bottom.width - 24) / 2));
    return {
        m_bottom.x + 8 + static_cast<int>(tab) * (width + 8),
        m_bottom.y + 6,
        width,
        std::min(28.0, std::max(0.0, m_bottom.height - 12))
    };
}

CanvasViewport UI::fileBounds() const
{
    return {0, 0, std::min(64.0, m_bar.width), m_bar.height};
}

CanvasViewport UI::fileMenuBounds() const
{
    return {0, m_bar.height, std::min(180.0, m_bar.width), std::min(92.0, m_panel.height)};
}

CanvasViewport UI::fileOptionBounds(int index) const
{
    if (index < 0 || index >= 3)
        return {};
    const auto menu = fileMenuBounds();
    return {
        menu.x + 1,
        menu.y + 1 + index * 30,
        std::max(0.0, menu.width - 2),
        std::clamp(menu.height - 1 - index * 30, 0.0, 30.0)
    };
}

void UI::closeFileMenu(Input& input)
{
    if (m_fileMenuOpen)
    {
        m_fileMenuOpen = false;
        input.setUiCapture({});
        input.setCanvasFocused(true);
    }
}

std::optional<UI::FileCommand> UI::takeFileCommand()
{
    return std::exchange(m_fileCommand, std::nullopt);
}

void UI::setFileStatus(std::string message, bool error)
{
    m_fileStatus = std::move(message);
    m_fileError = error;
}

void UI::dismissPopups(Input& input)
{
    closeFileMenu(input);
    cancel(input);
    closeInfo(input);
    m_circuitTabs.cancel(input);
}

bool UI::handleFileMenu(const UiInputEvent& event, Input& input)
{
    if (event.kind == UiInputKind::WindowFocus && !event.focused)
    {
        closeFileMenu(input);
        m_fileMousePressed = m_fileEscapePressed = false;
        return false;
    }
    if (event.kind == UiInputKind::MouseButton && event.action == GLFW_RELEASE &&
        m_fileMousePressed)
    {
        m_fileMousePressed = false;
        input.setCanvasFocused(true);
        return true;
    }
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_ESCAPE &&
        (m_fileMenuOpen || m_fileEscapePressed))
    {
        closeFileMenu(input);
        m_fileEscapePressed = event.action != GLFW_RELEASE;
        return true;
    }
    if (m_fileMenuOpen)
    {
        if (event.kind == UiInputKind::MouseButton && event.action == GLFW_PRESS)
        {
            m_fileMousePressed = true;
            if (event.code == GLFW_MOUSE_BUTTON_LEFT)
                for (int index = 0; index < 3; ++index)
                    if (fileOptionBounds(index).contains(event.x, event.y))
                        m_fileCommand = static_cast<FileCommand>(index);
            closeFileMenu(input);
        }
        return true;
    }
    if (event.kind == UiInputKind::MouseButton && m_bar.contains(event.x, event.y))
    {
        cancel(input);
        closeInfo(input);
        m_circuitTabs.cancel(input);
        if (event.action == GLFW_PRESS)
        {
            input.cancelCurrentAction();
            m_fileMousePressed = true;
            if (event.code == GLFW_MOUSE_BUTTON_LEFT && fileBounds().contains(event.x, event.y))
            {
                m_fileMenuOpen = true;
                input.setUiCapture({true, true});
            }
        }
        if (event.action == GLFW_RELEASE)
            input.setCanvasFocused(true);
        return true;
    }
    return (event.kind == UiInputKind::Cursor || event.kind == UiInputKind::Scroll) &&
           m_bar.contains(m_pointer.x, m_pointer.y);
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
    if (dragging() || m_nameEditing)
    {
        m_dragDefinition.clear();
        input.setUiCapture({});
        if (m_nameEditing)
            input.setCanvasFocused(true);
        m_nameEditing = false;
        m_nameDraft.clear();
        m_nameError.clear();
    }
}

void UI::closeInfo(Input& input)
{
    if (m_nameEditing)
        cancel(input);
    m_infoComponent = -1;
    m_infoScroll = 0;
}

bool UI::canName(const Scene& scene) const
{
    const auto* view = scene.getCommittedComponentView(m_infoComponent);
    const auto* definition =
        view ? scene.getComponentCatalog().find(view->getDefinitionIdentity().id) : nullptr;
    return definition && (std::holds_alternative<ManualInputBehavior>(definition->behavior) ||
                          std::holds_alternative<OutputBehavior>(definition->behavior));
}

CanvasViewport UI::nameBounds(const Scene& scene) const
{
    if (!canName(scene))
        return {};
    const auto bounds = infoBounds(scene);
    return {bounds.x + 10, bounds.y + infoHeaderHeight + 18, bounds.width - 20, 28};
}

std::vector<std::string> UI::componentInfo(const Scene& scene) const
{
    const auto* view = scene.getCommittedComponentView(m_infoComponent);
    if (!view)
        return {};
    const auto* definition = scene.getComponentCatalog().find(view->getDefinitionIdentity().id);
    std::vector<std::string> lines{definition ? definition->displayName : "Component"};
    if (!canName(scene) && !view->getBodyLabel().empty() && view->getBodyLabel() != lines.front())
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
        infoHeaderHeight + infoFooterHeight + (canName(scene) ? infoNameHeight : 0) +
            static_cast<double>(lines.size() - 1) * infoRowHeight,
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
            (infoBounds(scene).height - infoHeaderHeight - infoFooterHeight -
             (canName(scene) ? infoNameHeight : 0)) /
            infoRowHeight
        )
    );
}

bool UI::handleInput(
    const UiInputEvent& event, Scene& scene, Input& input, const CanvasCameraFrame& camera
)
{
    if (event.kind == UiInputKind::Cursor || event.kind == UiInputKind::MouseButton)
        m_pointer = {event.x, event.y};
    if (!m_circuitTabs.confirmingDelete() && handleFileMenu(event, input))
        return true;
    if (event.kind == UiInputKind::MouseButton && event.action == GLFW_PRESS &&
        m_circuitTabs.contains(event.x, event.y))
    {
        cancel(input);
        closeInfo(input);
        m_message.clear();
    }
    if (!dragging() && m_circuitTabs.handleInput(event, input))
        return true;
    if (m_infoComponent != -1 && !scene.getCommittedComponentView(m_infoComponent))
        closeInfo(input);
    if (event.kind == UiInputKind::WindowFocus)
    {
        if (!event.focused)
        {
            cancel(input);
            closeInfo(input);
            m_infoRightPressed = m_infoEscapePressed = false;
        }
        return false;
    }
    if (event.kind == UiInputKind::Cursor || event.kind == UiInputKind::MouseButton)
        m_pointer = {event.x, event.y};
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_F2 && !dragging() &&
        !m_nameEditing)
        input.setCanvasFocused(true);
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_ESCAPE &&
        (m_infoComponent != -1 || m_infoEscapePressed))
    {
        closeInfo(input);
        m_infoEscapePressed = event.action != GLFW_RELEASE;
        if (!m_infoEscapePressed)
            input.setCanvasFocused(true);
        return true;
    }
    if (m_nameEditing && event.kind == UiInputKind::Key)
    {
        if (event.action == GLFW_PRESS || event.action == GLFW_REPEAT)
        {
            if (event.code == GLFW_KEY_BACKSPACE && !m_nameDraft.empty())
                m_nameDraft.pop_back();
            else if (event.code == GLFW_KEY_ENTER || event.code == GLFW_KEY_KP_ENTER)
            {
                const auto result = EditorActions(scene).apply({ConfigureComponentProperties{
                    .componentId = m_infoComponent, .label = m_nameDraft
                }});
                if (result)
                    cancel(input);
                else
                    m_nameError = result.message;
            }
        }
        return true;
    }
    if (m_nameEditing && event.kind == UiInputKind::Text)
    {
        // TextPainter currently supports printable ASCII; keep this small field consistent.
        if (event.codepoint >= 32 && event.codepoint <= 126 && m_nameDraft.size() < 32)
            m_nameDraft.push_back(static_cast<char>(event.codepoint));
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
            {
                if (m_canCreate && event.code == GLFW_MOUSE_BUTTON_LEFT &&
                    event.action == GLFW_PRESS && nameBounds(scene).contains(event.x, event.y) &&
                    !m_nameEditing)
                {
                    m_nameDraft = scene.getCommittedComponentView(m_infoComponent)->getBodyLabel();
                    m_nameError.clear();
                    m_nameEditing = true;
                    input.setUiCapture({true, false});
                }
                return true;
            }
            if (event.action == GLFW_PRESS)
            {
                closeInfo(input);
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
        if (m_bottom.contains(event.x, event.y))
        {
            if (event.action == GLFW_PRESS)
            {
                input.cancelCurrentAction();
                if (event.code == GLFW_MOUSE_BUTTON_LEFT)
                    for (const auto tab : {BottomTab::First, BottomTab::Second})
                        if (bottomTabBounds(tab).contains(event.x, event.y))
                            m_bottomTab = tab;
            }
            if (event.action == GLFW_RELEASE)
                input.setCanvasFocused(true);
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
    return dragging() ||
           ((event.kind == UiInputKind::Cursor || event.kind == UiInputKind::Scroll) &&
            m_bottom.contains(m_pointer.x, m_pointer.y)) ||
           (event.kind == UiInputKind::Cursor && m_panel.contains(event.x, event.y));
}
