#include "Editor/Input.h"
#include "Geometry/GridSystem.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>

namespace
{
constexpr std::array edits{
    Input::EditCommand::Undo,
    Input::EditCommand::Redo,
    Input::EditCommand::Copy,
    Input::EditCommand::Paste,
    Input::EditCommand::Delete,
    Input::EditCommand::SelectAll
};
}

CanvasViewport UI::menuBounds(Menu menu) const
{
    const double x = static_cast<int>(menu) * 64.0;
    const double width = menu == Menu::Simulation ? 120.0 : 64.0;
    return {x, 0, std::clamp(m_bar.width - x, 0.0, width), m_bar.height};
}

double UI::navigationStatusX() const
{
    const auto last = menuBounds(Menu::Simulation);
    return last.x + last.width + 12;
}

CanvasViewport UI::dropdownBounds(Menu menu) const
{
    const double width = std::min(menu == Menu::File ? 300.0 : 320.0, m_bar.width);
    return {
        std::min(menuBounds(menu).x, std::max(0.0, m_bar.width - width)),
        m_bar.height,
        width,
        std::min(2.0 + menuOptions(menu).size() * 30, m_panel.height)
    };
}

CanvasViewport UI::menuOptionBounds(Menu menu, int index) const
{
    if (index < 0 || index >= static_cast<int>(menuOptions(menu).size()))
        return {};
    const auto bounds = dropdownBounds(menu);
    return {
        bounds.x + 1,
        bounds.y + 1 + index * 30,
        std::max(0.0, bounds.width - 2),
        std::clamp(bounds.height - 1 - index * 30, 0.0, 30.0)
    };
}

CanvasViewport UI::fileBounds() const
{
    return menuBounds(Menu::File);
}

CanvasViewport UI::fileMenuBounds() const
{
    return dropdownBounds(Menu::File);
}

CanvasViewport UI::fileOptionBounds(int index) const
{
    return menuOptionBounds(Menu::File, index);
}

std::vector<UI::MenuItem> UI::menuOptions(Menu menu) const
{
    if (menu == Menu::Edit)
        return m_editOptions;
    if (menu == Menu::View)
        return m_viewOptions;
    if (menu == Menu::Simulation)
        return {{"Reset Simulation", "", !m_readOnly}};
    std::vector<MenuItem> items;
    for (const auto& name : fileOptions())
        items.push_back({name, "", !m_readOnly});
    return items;
}

void UI::refreshMenuOptions(const Input& input)
{
    m_editOptions = {
        {"Undo", "Ctrl+Z"},
        {"Redo", "Ctrl+Y / Ctrl+Shift+Z"},
        {"Copy", "Ctrl+C"},
        {"Paste", "Ctrl+V"},
        {"Delete", "Del / Backspace"},
        {"Select All", "Ctrl+A"}
    };
    for (std::size_t i = 0; i < edits.size(); ++i)
        m_editOptions[i].enabled = input.canEditCommand(edits[i]);
    m_viewOptions = {
        {"Fit Circuit", "Home", input.canViewCommand(Input::ViewCommand::FitCircuit)},
        {"Center Selection", "F", input.canViewCommand(Input::ViewCommand::CenterSelection)},
        {"Reset Zoom", "Ctrl+0"},
        {"Lower Panel", "Ctrl+J", true, m_bottomVisible},
        {"Debug Overlay", "F3", true, m_debugVisible}
    };
}

void UI::executeMenuOption(Menu menu, int index, Input& input)
{
    if (menu == Menu::Edit)
    {
        if (index >= 0 && index < static_cast<int>(edits.size()))
            input.executeEditCommand(
                edits[index], GridSystem::worldToGrid(input.getPanOffset()), true
            );
        return;
    }
    if (menu == Menu::Simulation)
    {
        if (m_readOnly)
            return;
        if (index == 0)
        {
            input.resetSimulation();
            m_simulationReset = true;
            clockTimeline().clear();
            m_clockFollow = true;
            m_clockEnd = clockWindowSeconds();
            m_clockHover = -1;
            m_clockError.clear();
        }
        return;
    }
    if (menu != Menu::View)
        return;
    switch (index)
    {
    case 0:
        input.executeViewCommand(Input::ViewCommand::FitCircuit, m_surface);
        break;
    case 1:
        input.executeViewCommand(Input::ViewCommand::CenterSelection, m_surface);
        break;
    case 2:
        input.executeViewCommand(Input::ViewCommand::ResetZoom, m_surface);
        break;
    case 3:
        finishBottomResize(input);
        closeClockEdit(input);
        m_clockHover = -1;
        m_bottomVisible = !m_bottomVisible;
        layoutBottom(input);
        if (m_bottomVisible && m_bottomTab == BottomTab::Messages)
            messageState().unread = false;
        break;
    case 4:
        m_debugVisible = !m_debugVisible;
        break;
    }
}

void UI::closeMenu(Input& input)
{
    if (!m_openMenu)
        return;
    m_openMenu.reset();
    input.setUiCapture({});
    input.setCanvasFocused(true);
}

bool UI::handleMenus(const UiInputEvent& event, Input& input)
{
    if (event.kind == UiInputKind::WindowFocus && !event.focused)
    {
        closeMenu(input);
        m_menuCommand.reset();
        m_fileMousePressed = m_fileEscapePressed = false;
        m_menuShortcutKey = -1;
        return false;
    }
    if (event.kind == UiInputKind::Key && event.code == m_menuShortcutKey)
    {
        if (event.action == GLFW_RELEASE)
        {
            m_menuShortcutKey = -1;
            if (event.code != GLFW_KEY_F3)
                input.setCanvasFocused(true);
        }
        return event.code != GLFW_KEY_F3;
    }
    if (event.kind == UiInputKind::Key && !m_openMenu && input.isCanvasFocused() &&
        !input.getUiCapture().keyboard && event.action == GLFW_PRESS)
    {
        const int modifiers = event.modifiers;
        if ((event.code == GLFW_KEY_F3 && modifiers == 0) ||
            (event.code == GLFW_KEY_J && modifiers == GLFW_MOD_CONTROL))
        {
            executeMenuOption(Menu::View, event.code == GLFW_KEY_J ? 3 : 4, input);
            m_menuShortcutKey = event.code;
            return event.code != GLFW_KEY_F3;
        }
    }
    if (event.kind == UiInputKind::MouseButton && event.action == GLFW_RELEASE &&
        m_fileMousePressed)
    {
        m_fileMousePressed = false;
        input.setCanvasFocused(true);
        return true;
    }
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_ESCAPE &&
        (m_openMenu || m_fileEscapePressed))
    {
        closeMenu(input);
        m_fileEscapePressed = event.action != GLFW_RELEASE;
        if (!m_fileEscapePressed)
            input.setCanvasFocused(true);
        return true;
    }
    if (event.kind == UiInputKind::MouseButton && event.code == GLFW_MOUSE_BUTTON_LEFT &&
        event.action == GLFW_PRESS && messageNoticeBounds().contains(event.x, event.y))
    {
        dismissPopups(input);
        m_bottomVisible = true;
        layoutBottom(input);
        m_bottomTab = BottomTab::Messages;
        messageState().unread = false;
        m_fileMousePressed = true;
        input.setCanvasFocused(false);
        return true;
    }
    const auto headerAt = [&]() -> std::optional<Menu>
    {
        for (const auto menu : {Menu::File, Menu::Edit, Menu::View, Menu::Simulation})
            if (menuBounds(menu).contains(m_pointer.x, m_pointer.y))
                return menu;
        return {};
    };
    if (m_openMenu && event.kind == UiInputKind::Cursor)
    {
        if (const auto header = headerAt())
            m_openMenu = header;
        return true;
    }
    if (event.kind == UiInputKind::MouseButton && event.action == GLFW_PRESS)
    {
        if (const auto header = headerAt())
        {
            const bool close = m_openMenu == header;
            dismissPopups(input);
            m_fileMousePressed = true;
            if (!close && event.code == GLFW_MOUSE_BUTTON_LEFT)
            {
                m_openMenu = header;
                input.setUiCapture({true, true});
            }
            return true;
        }
    }
    if (m_openMenu)
    {
        if (event.kind == UiInputKind::MouseButton && event.action == GLFW_PRESS)
        {
            const auto menu = *m_openMenu;
            m_fileMousePressed = true;
            if (event.code == GLFW_MOUSE_BUTTON_LEFT)
            {
                const auto options = menuOptions(menu);
                for (int i = 0; i < static_cast<int>(options.size()); ++i)
                    if (options[i].enabled && menuOptionBounds(menu, i).contains(event.x, event.y))
                    {
                        if (menu == Menu::File)
                            m_fileCommand = fileCommands().at(i);
                        else
                            m_menuCommand = std::pair{menu, i};
                    }
            }
            closeMenu(input);
        }
        return true;
    }
    if (event.kind == UiInputKind::MouseButton && m_bar.contains(event.x, event.y))
    {
        dismissPopups(input);
        if (event.action == GLFW_PRESS)
            m_fileMousePressed = true;
        return true;
    }
    return (event.kind == UiInputKind::Cursor || event.kind == UiInputKind::Scroll) &&
           m_bar.contains(m_pointer.x, m_pointer.y);
}
