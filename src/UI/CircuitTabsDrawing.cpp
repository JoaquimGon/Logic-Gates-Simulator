#include "Editor/CircuitViews.h"
#include "Graphics/Renderer.h"
#include "UI/CircuitTabs.h"

#include <algorithm>
#include <vector>

void CircuitTabs::draw(Renderer& renderer) const
{
    if (!enabled() || m_bar.width <= 0 || m_bar.height <= 0)
        return;
    const glm::vec4 ink{0.87f, 0.91f, 0.97f, 1};
    std::vector<TextRun> text;
    auto label = [&](const std::string& value, CanvasViewport bounds, float scale)
    {
        const float width = getTextWidth(value, 1, *m_font);
        if (width > 0)
            scale = std::min(scale, static_cast<float>(std::max(0.0, bounds.width - 20)) / width);
        text.push_back(
            {value,
             {static_cast<float>(bounds.x + 10),
              static_cast<float>(bounds.y + bounds.height / 2) + getCapHeight(scale, *m_font) / 2},
             scale,
             ink}
        );
    };
    auto icon = [&](const std::string& value, CanvasViewport bounds, float scale)
    {
        const float width = getTextWidth(value, 1, *m_font);
        if (width > 0)
            scale = std::min(scale, static_cast<float>(std::max(0.0, bounds.width - 10)) / width);
        text.push_back(
            {value,
             {static_cast<float>(bounds.x + (bounds.width - width * scale) / 2),
              static_cast<float>(bounds.y + bounds.height / 2) + getCapHeight(scale, *m_font) / 2},
             scale,
             ink}
        );
    };
    renderer.drawScreenRect(m_bar, {0.075f, 0.09f, 0.12f, 1});
    auto tab = [&](std::size_t index)
    {
        auto bounds = tabBounds(index);
        const bool active = index == m_views->activeIndex();
        renderer.drawScreenRect(
            bounds, active ? glm::vec4{0.16f, 0.3f, 0.46f, 1} : glm::vec4{0.11f, 0.15f, 0.21f, 1}
        );
        renderer.drawScreenRect(
            {bounds.x + bounds.width - 1, bounds.y, 1, bounds.height}, {0.18f, 0.23f, 0.3f, 1}
        );
        if (active)
            renderer.drawScreenRect(
                {bounds.x, bounds.y + bounds.height - 2, bounds.width, 2}, {0.3f, 0.65f, 0.95f, 1}
            );
        if (index > 0)
        {
            const auto close = closeBounds(index);
            if (close.contains(m_pointerX, m_pointerY))
                renderer.drawScreenRect(close, {0.5f, 0.17f, 0.18f, 1});
            icon("X", close, 0.38f);
            bounds.width -= close.width;
        }
        label(tabName(index), bounds, 0.42f);
    };
    renderer.setScreenClip(m_tabList);
    for (std::size_t index = 1; index < m_views->size(); ++index)
    {
        const auto bounds = tabBounds(index);
        if (bounds.x + bounds.width > m_tabList.x && bounds.x < m_tabList.x + m_tabList.width)
            tab(index);
    }
    const auto add = addBounds();
    renderer.drawScreenRect(add, {0.18f, 0.23f, 0.3f, 1});
    renderer.drawScreenRect(
        {add.x + 1, add.y + 1, std::max(0.0, add.width - 2), std::max(0.0, add.height - 2)},
        add.contains(m_pointerX, m_pointerY) ? glm::vec4{0.16f, 0.3f, 0.46f, 1}
                                             : glm::vec4{0.11f, 0.15f, 0.21f, 1}
    );
    icon("+", add, 0.55f);
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(m_bar);
    tab(0);
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(std::nullopt);
    if (m_popup)
    {
        const auto popup = popupBounds();
        renderer.drawScreenRect(popup, {0.3f, 0.45f, 0.6f, 1});
        renderer.drawScreenRect(
            {popup.x + 1, popup.y + 1, popup.width - 2, popup.height - 2}, {0.08f, 0.12f, 0.17f, 1}
        );
        if (m_confirmDelete)
        {
            label("Delete circuit?", {popup.x, popup.y, popup.width, 30}, 0.5f);
            label(m_views->name(*m_popup), {popup.x, popup.y + 32, popup.width, 28}, 0.43f);
            const auto cancel = cancelDeleteBounds();
            const auto remove = deleteBounds();
            renderer.drawScreenRect(cancel, {0.16f, 0.3f, 0.46f, 1});
            renderer.drawScreenRect(remove, {0.55f, 0.16f, 0.18f, 1});
            label("Cancel", cancel, 0.42f);
            label("Delete", remove, 0.42f);
        }
        else
        {
            label(m_views->name(*m_popup), {popup.x, popup.y, popup.width, 34}, 0.5f);
            const auto field = nameBounds();
            renderer.drawScreenRect(
                field,
                m_editing ? glm::vec4{0.3f, 0.65f, 0.95f, 1} : glm::vec4{0.25f, 0.35f, 0.46f, 1}
            );
            renderer.drawScreenRect(
                {field.x + 1, field.y + 1, field.width - 2, field.height - 2},
                m_editing && m_selectAll ? glm::vec4{0.16f, 0.3f, 0.46f, 1}
                                         : glm::vec4{0.06f, 0.09f, 0.13f, 1}
            );
            label(m_editing ? m_draft + "|" : m_views->name(*m_popup), field, 0.43f);
            label(
                m_editing ? "Enter: save; Esc: cancel" : "Click name to edit; Esc: close",
                {popup.x, popup.y + 80, popup.width, 26},
                0.33f
            );
        }
        renderer.drawText(text, TextSpace::Screen);
    }
}
