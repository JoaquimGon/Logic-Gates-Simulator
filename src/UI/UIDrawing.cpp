#include "Editor/Scene.h"
#include "Geometry/GridSystem.h"
#include "Graphics/Renderer.h"
#include "UI/UI.h"

#include <algorithm>

void UI::draw(Renderer& renderer, const Scene& scene, const CanvasCameraFrame& camera) const
{
    if (m_surface.windowWidth <= 0 || m_surface.windowHeight <= 0 || m_panel.width < 30)
        return;
    std::vector<TextRun> text;
    const glm::vec4 ink{0.87f, 0.91f, 0.97f, 1};
    auto label = [&](const std::string& value, CanvasViewport bounds, float scale, glm::vec4 color)
    {
        const auto& font = renderer.fontMetrics();
        const float width = getTextWidth(value, 1, font);
        if (width > 0)
            scale = std::min(scale, static_cast<float>(std::max(0.0, bounds.width - 20)) / width);
        text.push_back(
            {value,
             {static_cast<float>(bounds.x + 10),
              static_cast<float>(bounds.y + bounds.height / 2) + getCapHeight(scale, font) / 2},
             scale,
             color}
        );
    };
    // These same two primitives serve panel backgrounds, buttons, and the drag badge.
    renderer.drawScreenRect(m_panel, {0.075f, 0.09f, 0.12f, 1});
    renderer.drawScreenRect({m_panel.width - 1, 0, 1, m_panel.height}, {0.18f, 0.23f, 0.3f, 1});
    label("COMPONENTS", {6, 12, m_panel.width - 12, 26}, 0.65f, ink);
    label(
        "Drag a button to place", {6, 42, m_panel.width - 12, 18}, 0.4f, {0.55f, 0.65f, 0.76f, 1}
    );
    for (const auto& button : m_buttons)
    {
        const auto& rect = button.bounds;
        if (rect.y < m_list.y || rect.y + rect.height > m_list.y + m_list.height)
            continue;
        const bool hover = m_canCreate && rect.contains(m_pointer.x, m_pointer.y);
        renderer.drawScreenRect(
            rect, hover ? glm::vec4{0.3f, 0.65f, 0.95f, 1} : glm::vec4{0.19f, 0.26f, 0.35f, 1}
        );
        renderer.drawScreenRect(
            {rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2},
            hover ? glm::vec4{0.14f, 0.25f, 0.38f, 1} : glm::vec4{0.11f, 0.15f, 0.21f, 1}
        );
        label(button.label, rect, 0.5f, m_canCreate ? ink : glm::vec4{0.42f, 0.48f, 0.56f, 1});
    }
    const auto footer = !m_message.empty() ? m_message
                        : !m_canCreate     ? "F2: switch to Selection"
                        : m_maxScroll > 0  ? "Scroll for more components"
                                           : "Release on the canvas";
    label(
        footer,
        {6, m_panel.height - 36, m_panel.width - 12, 22},
        0.35f,
        m_message.empty() ? glm::vec4{0.55f, 0.65f, 0.76f, 1} : glm::vec4{1, 0.55f, 0.4f, 1}
    );
    if (dragging())
    {
        const auto* definition = scene.getComponentCatalog().find(m_dragDefinition);
        if (const auto position = dropPosition(camera))
            renderer.drawComponentBoundingBox(
                GridSystem::gridToWorld(*position),
                {definition->layout.width, definition->layout.height},
                0,
                0.8f
            );
        const CanvasViewport badge{m_pointer.x + 14, m_pointer.y + 14, 170, 34};
        renderer.drawScreenRect(badge, {0.13f, 0.28f, 0.43f, 0.95f});
        label(definition->displayName, badge, 0.5f, ink);
    }
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    const auto info = componentInfo(scene);
    if (!info.empty())
    {
        const auto bounds = infoBounds(scene);
        renderer.drawScreenRect(bounds, {0.3f, 0.45f, 0.6f, 1});
        renderer.drawScreenRect(
            {bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2},
            {0.08f, 0.12f, 0.17f, 1}
        );
        label(info.front(), {bounds.x, bounds.y, bounds.width, infoHeaderHeight}, 0.6f, ink);
        const int rows = infoVisibleRows(scene);
        const int offset =
            std::clamp(m_infoScroll, 0, std::max(0, static_cast<int>(info.size()) - 1 - rows));
        for (int row = 0; row < rows && row + offset + 1 < static_cast<int>(info.size()); ++row)
            label(
                info[row + offset + 1],
                {bounds.x + 4,
                 bounds.y + infoHeaderHeight + row * infoRowHeight,
                 bounds.width - 8,
                 infoRowHeight},
                0.43f,
                ink
            );
        label(
            static_cast<int>(info.size()) - 1 > rows ? "Scroll; Esc / outside to close"
                                                     : "Esc / click outside to close",
            {bounds.x, bounds.y + bounds.height - infoFooterHeight, bounds.width, infoFooterHeight},
            0.33f,
            {0.55f, 0.65f, 0.76f, 1}
        );
    }
    renderer.drawText(text, TextSpace::Screen);
}
