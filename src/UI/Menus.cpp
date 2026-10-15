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

CanvasViewport UI::examplesMenuBounds() const
{
    const auto parent = menuOptionBounds(Menu::File, static_cast<int>(fileCommands().size()));
    const auto menu = dropdownBounds(Menu::File);
    const double width = std::min(230.0, m_bar.width);
    const double height = std::min(2.0 + circuitExamples().size() * 30, m_panel.height);
    double x = menu.x + menu.width - 1, y = parent.y;
    if (x + width > m_bar.width)
    {
        if (menu.x >= width)
            x = menu.x - width + 1;
        else
        {
            x = std::clamp(menu.x, 0.0, std::max(0.0, m_bar.width - width));
            y = parent.y + parent.height;
        }
    }
    return {
        x,
        std::clamp(y, m_bar.height, std::max(m_bar.height, m_surface.windowHeight - height)),
        width,
        height
    };
}

CanvasViewport UI::exampleOptionBounds(int index) const
{
    if (index < 0 || index >= static_cast<int>(circuitExamples().size()))
        return {};
    const auto menu = examplesMenuBounds();
    return {
        menu.x + 1,
        menu.y + 1 + index * 30,
        std::max(0.0, menu.width - 2),
        std::clamp(menu.height - 1 - index * 30, 0.0, 30.0)
    };
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
    items.push_back({"Examples", ">"});
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
    m_examplesOpen = false;
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
        if (event.action == GLFW_PRESS)
        {
            if (examplesMenuOpen())
                m_examplesOpen = false;
            else
                closeMenu(input);
        }
        m_fileEscapePressed = event.action != GLFW_RELEASE;
        if (!m_fileEscapePressed && !m_openMenu)
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
        {
            if (m_openMenu != header)
                m_examplesOpen = false;
            m_openMenu = header;
        }
        else if (*m_openMenu == Menu::File)
        {
            const auto parent =
                menuOptionBounds(Menu::File, static_cast<int>(fileCommands().size()));
            if (parent.contains(m_pointer.x, m_pointer.y))
                m_examplesOpen = true;
            else if (
                dropdownBounds(Menu::File).contains(m_pointer.x, m_pointer.y) &&
                (!m_examplesOpen || !examplesMenuBounds().contains(m_pointer.x, m_pointer.y))
            )
                m_examplesOpen = false;
        }
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
                if (examplesMenuOpen())
                    for (int i = 0; i < static_cast<int>(circuitExamples().size()); ++i)
                        if (exampleOptionBounds(i).contains(event.x, event.y))
                        {
                            m_example = circuitExamples()[i].id;
                            closeMenu(input);
                            return true;
                        }
                const auto options = menuOptions(menu);
                for (int i = 0; i < static_cast<int>(options.size()); ++i)
                    if (options[i].enabled && menuOptionBounds(menu, i).contains(event.x, event.y))
                    {
                        if (menu == Menu::File)
                        {
                            const auto commands = fileCommands();
                            if (i == static_cast<int>(commands.size()))
                            {
                                m_examplesOpen = true;
                                return true;
                            }
                            m_fileCommand = commands.at(i);
                        }
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
