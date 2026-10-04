#include "UI/CircuitTabs.h"

#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Graphics/Text/TextGeometry.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace
{
constexpr float titleScale = 0.42f;
}

void CircuitTabs::bind(CircuitViews* views, const FontMetrics* font)
{
    m_views = views;
    m_font = font;
    m_hover.reset();
    m_popup.reset();
    m_lastActive.reset();
    m_scroll = 0;
    m_editing = m_selectAll = m_escapePressed = false;
    m_confirmDelete = false;
}

double CircuitTabs::maxScroll() const
{
    return std::max(0.0, (m_views->size() - 1) * m_tabWidth + addBounds().width - m_tabList.width);
}

void CircuitTabs::layout(CanvasViewport bar)
{
    const bool resized = bar != m_bar;
    m_bar = bar;
    if (!enabled())
        return;
    m_tabWidth = static_cast<double>(getTextWidth("unnamed", titleScale, *m_font)) + 44;
    const double mainWidth = std::min(m_tabWidth, std::max(0.0, bar.width - bar.height));
    m_tabList = {bar.x + mainWidth, bar.y, std::max(0.0, bar.width - mainWidth), bar.height};
    m_scroll = std::clamp(m_scroll, 0.0, maxScroll());
    if (resized || m_lastActive != m_views->activeIndex())
    {
        if (m_views->activeIndex() > 0)
        {
            const double left = (m_views->activeIndex() - 1) * m_tabWidth;
            const double right =
                left + m_tabWidth +
                (m_views->activeIndex() == m_views->size() - 1 ? addBounds().width : 0);
            if (left < m_scroll)
                m_scroll = left;
            if (right > m_scroll + m_tabList.width)
                m_scroll = right - m_tabList.width;
            m_scroll = std::clamp(m_scroll, 0.0, maxScroll());
        }
        m_lastActive = m_views->activeIndex();
    }
}

CanvasViewport CircuitTabs::tabBounds(std::size_t index) const
{
    if (index == 0)
        return {m_bar.x, m_bar.y, m_tabList.x - m_bar.x, m_bar.height};
    return {m_tabList.x + (index - 1) * m_tabWidth - m_scroll, m_bar.y, m_tabWidth, m_bar.height};
}

CanvasViewport CircuitTabs::addBounds() const
{
    if (!enabled())
        return {};
    const double side = std::min(m_bar.height, m_tabList.width);
    return {m_tabList.x + (m_views->size() - 1) * m_tabWidth - m_scroll, m_bar.y, side, side};
}

CanvasViewport CircuitTabs::popupBounds() const
{
    if (!m_popup)
        return {};
    const double width = std::min(340.0, m_bar.width);
    const double x =
        std::clamp(tabBounds(*m_popup).x, m_bar.x, m_bar.x + std::max(0.0, m_bar.width - width));
    return {x, m_bar.y + m_bar.height, width, 112};
}

CanvasViewport CircuitTabs::closeBounds(std::size_t index) const
{
    if (!enabled() || index == 0 || index >= m_views->size())
        return {};
    const auto tab = tabBounds(index);
    const double side = std::min(24.0, tab.height);
    return {tab.x + tab.width - side - 2, tab.y + (tab.height - side) / 2, side, side};
}

CanvasViewport CircuitTabs::nameBounds() const
{
    const auto popup = popupBounds();
    return {popup.x + 10, popup.y + 42, std::max(0.0, popup.width - 20), 30};
}

CanvasViewport CircuitTabs::deleteBounds() const
{
    const auto popup = popupBounds();
    return {popup.x + popup.width / 2 + 4, popup.y + 72, std::max(0.0, popup.width / 2 - 14), 30};
}

CanvasViewport CircuitTabs::cancelDeleteBounds() const
{
    const auto popup = popupBounds();
    return {popup.x + 10, popup.y + 72, std::max(0.0, popup.width / 2 - 14), 30};
}

bool CircuitTabs::contains(double x, double y) const
{
    return enabled() && (m_bar.contains(x, y) || (m_popup && popupBounds().contains(x, y)));
}

std::optional<std::size_t> CircuitTabs::tabAt(double x, double y) const
{
    if (!m_bar.contains(x, y) || addBounds().contains(x, y))
        return std::nullopt;
    if (tabBounds(0).contains(x, y))
        return 0;
    if (!m_tabList.contains(x, y))
        return std::nullopt;
    const auto index = static_cast<std::size_t>((x - m_tabList.x + m_scroll) / m_tabWidth) + 1;
    return index < m_views->size() ? std::optional{index} : std::nullopt;
}

std::string CircuitTabs::tabName(std::size_t index) const
{
    std::string name = m_views->name(index);
    const double width = std::max(0.0, tabBounds(index).width - 20 - (index == 0 ? 0 : 24));
    if (getTextWidth(name, titleScale, *m_font) <= width)
        return name;
    while (!name.empty() && getTextWidth(name + "...", titleScale, *m_font) > width)
        name.pop_back();
    return name + "...";
}

void CircuitTabs::stopEditing(Input& input)
{
    if (m_editing)
    {
        m_editing = false;
        input.setUiCapture({});
        input.setCanvasFocused(true);
    }
}

void CircuitTabs::cancel(Input& input)
{
    stopEditing(input);
    if (m_confirmDelete)
    {
        input.setUiCapture({});
        input.setCanvasFocused(true);
    }
    m_confirmDelete = false;
    m_popup.reset();
    m_hover.reset();
    m_escapePressed = false;
}

void CircuitTabs::open(std::size_t index)
{
    m_confirmDelete = false;
    m_popup = index;
    m_draft = m_views->name(index);
}

void CircuitTabs::update(double now, Input& input)
{
    m_now = now;
    if (!enabled() || m_editing || m_confirmDelete || input.getUiCapture().keyboard ||
        input.getUiCapture().pointer)
        return;
    if (m_hover && now - m_hoverSince >= 1.0 && m_popup != m_hover)
        open(*m_hover);
    if (m_popup && !m_hover && !popupBounds().contains(m_pointerX, m_pointerY))
        m_popup.reset();
}

bool CircuitTabs::handleInput(const UiInputEvent& event, Input& input)
{
    if (!enabled())
        return false;
    if (event.kind == UiInputKind::WindowFocus && !event.focused)
    {
        cancel(input);
        return false;
    }
    if (event.kind == UiInputKind::Cursor || event.kind == UiInputKind::MouseButton)
    {
        m_pointerX = event.x;
        m_pointerY = event.y;
    }
    if (event.kind == UiInputKind::Cursor)
    {
        if (m_confirmDelete)
            return true;
        const auto hover = tabAt(event.x, event.y);
        if (hover != m_hover)
        {
            m_hover = hover;
            m_hoverSince = m_now;
        }
        return contains(event.x, event.y);
    }
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_ESCAPE &&
        (m_popup || m_escapePressed))
    {
        cancel(input);
        m_escapePressed = event.action != GLFW_RELEASE;
        input.setCanvasFocused(true);
        return true;
    }
    if (m_confirmDelete)
    {
        if (event.kind == UiInputKind::MouseButton && event.action == GLFW_PRESS)
        {
            if (event.code == GLFW_MOUSE_BUTTON_LEFT && deleteBounds().contains(event.x, event.y))
            {
                const auto index = *m_popup;
                cancel(input);
                m_views->remove(index, input);
                m_lastActive.reset();
                layout(m_bar);
            }
            else if (
                cancelDeleteBounds().contains(event.x, event.y) ||
                !popupBounds().contains(event.x, event.y)
            )
                cancel(input);
        }
        return true;
    }
    if (m_editing && event.kind == UiInputKind::Key)
    {
        if (event.action == GLFW_PRESS || event.action == GLFW_REPEAT)
        {
            if (event.code == GLFW_KEY_A && (event.modifiers & GLFW_MOD_CONTROL))
                m_selectAll = true;
            else if (event.code == GLFW_KEY_BACKSPACE)
            {
                if (m_selectAll)
                    m_draft.clear();
                else if (!m_draft.empty())
                    m_draft.pop_back();
                m_selectAll = false;
            }
            else if (event.code == GLFW_KEY_ENTER || event.code == GLFW_KEY_KP_ENTER)
            {
                m_views->rename(*m_popup, m_draft);
                stopEditing(input);
                m_draft = m_views->name(*m_popup);
            }
        }
        return true;
    }
    if (m_editing && event.kind == UiInputKind::Text)
    {
        if (event.codepoint >= 32 && event.codepoint <= 126)
        {
            if (m_selectAll)
                m_draft.clear();
            m_selectAll = false;
            if (m_draft.size() < 64)
                m_draft.push_back(static_cast<char>(event.codepoint));
        }
        return true;
    }
    if (event.kind == UiInputKind::MouseButton)
    {
        if (m_popup && popupBounds().contains(event.x, event.y))
        {
            if (event.code == GLFW_MOUSE_BUTTON_LEFT && event.action == GLFW_PRESS &&
                nameBounds().contains(event.x, event.y))
            {
                if (!m_editing)
                    m_draft = m_views->name(*m_popup);
                m_editing = m_selectAll = true;
                input.setUiCapture({true, false});
            }
            return true;
        }
        if (m_popup && event.action == GLFW_PRESS)
        {
            cancel(input);
            if (!m_bar.contains(event.x, event.y) && event.code == GLFW_MOUSE_BUTTON_LEFT)
                return true;
        }
        if (m_bar.contains(event.x, event.y))
        {
            if (event.action == GLFW_PRESS && event.code == GLFW_MOUSE_BUTTON_LEFT)
            {
                cancel(input);
                input.cancelCurrentAction();
                if (addBounds().contains(event.x, event.y))
                    m_views->create(input);
                else if (const auto index = tabAt(event.x, event.y))
                {
                    if (closeBounds(*index).contains(event.x, event.y))
                    {
                        m_popup = *index;
                        m_confirmDelete = true;
                        input.setUiCapture({true, true});
                    }
                    else
                        m_views->select(*index, input);
                }
                layout(m_bar);
            }
            else if (event.action == GLFW_PRESS && event.code == GLFW_MOUSE_BUTTON_RIGHT)
            {
                if (const auto index = tabAt(event.x, event.y))
                    open(*index);
            }
            if (event.action == GLFW_RELEASE)
                input.setCanvasFocused(true);
            return true;
        }
    }
    if (event.kind == UiInputKind::Scroll && contains(m_pointerX, m_pointerY))
    {
        if (m_bar.contains(m_pointerX, m_pointerY) && std::isfinite(event.y))
        {
            m_scroll = std::clamp(m_scroll - event.y * m_tabWidth, 0.0, maxScroll());
            m_hover.reset();
        }
        return true;
    }
    return false;
}
