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
    auto label = [&](const std::string& value,
                     CanvasViewport bounds,
                     float scale,
                     glm::vec4 color,
                     bool centered = false)
    {
        const auto& font = renderer.fontMetrics();
        const float width = getTextWidth(value, 1, font);
        if (width > 0)
            scale = std::min(scale, static_cast<float>(std::max(0.0, bounds.width - 20)) / width);
        text.push_back(
            {value,
             {static_cast<float>(
                  centered ? bounds.x + (bounds.width - width * scale) / 2 : bounds.x + 10
              ),
              static_cast<float>(bounds.y + bounds.height / 2) + getCapHeight(scale, font) / 2},
             scale,
             color}
        );
    };
    // Rectangles and text serve the panel, tabs, cards, custom rows and drag badge.
    renderer.drawScreenRect(m_panel, {0.075f, 0.09f, 0.12f, 1});
    renderer.drawScreenRect(
        {m_panel.width - 1, m_panel.y, 1, m_panel.height}, {0.18f, 0.23f, 0.3f, 1}
    );
    label("COMPONENTS", {6, m_panel.y + 12, m_panel.width - 12, 26}, 0.65f, ink);
    label(
        "Drag a component to place",
        {6, m_panel.y + 42, m_panel.width - 12, 18},
        0.4f,
        {0.55f, 0.65f, 0.76f, 1}
    );
    for (const auto tab : {Tab::Native, Tab::Custom})
    {
        const auto bounds = tabBounds(tab);
        renderer.drawScreenRect(
            bounds,
            tab == m_tab ? glm::vec4{0.16f, 0.3f, 0.46f, 1} : glm::vec4{0.11f, 0.15f, 0.21f, 1}
        );
        if (tab == m_tab)
            renderer.drawScreenRect(
                {bounds.x, bounds.y + bounds.height - 2, bounds.width, 2}, {0.3f, 0.65f, 0.95f, 1}
            );
        label(tab == Tab::Native ? "Native" : "Custom", bounds, 0.45f, ink, true);
    }
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(m_list);
    for (const auto& button : m_buttons)
    {
        const auto& rect = button.bounds;
        if (m_list.height <= 0 || rect.y + rect.height <= m_list.y ||
            rect.y >= m_list.y + m_list.height)
            continue;
        const bool hover = m_canCreate && m_list.contains(m_pointer.x, m_pointer.y) &&
                           rect.contains(m_pointer.x, m_pointer.y);
        renderer.drawScreenRect(
            rect, hover ? glm::vec4{0.3f, 0.65f, 0.95f, 1} : glm::vec4{0.19f, 0.26f, 0.35f, 1}
        );
        renderer.drawScreenRect(
            {rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2},
            hover ? glm::vec4{0.14f, 0.25f, 0.38f, 1} : glm::vec4{0.11f, 0.15f, 0.21f, 1}
        );
        const auto color = m_canCreate ? ink : glm::vec4{0.42f, 0.48f, 0.56f, 1};
        if (m_tab == Tab::Native)
        {
            const auto* definition = scene.getComponentCatalog().find(button.definitionId);
            if (!definition)
                continue;
            const CanvasViewport visual{rect.x + 8, rect.y + 8, rect.width - 16, rect.height - 36};
            const auto bounds = normalizedBodyBounds(definition->presentation.body);
            const float fit = static_cast<float>(std::min(
                visual.width / (definition->layout.width * bounds.width()),
                visual.height / (definition->layout.height * bounds.height())
            ));
            const glm::vec2 size{definition->layout.width * fit, definition->layout.height * fit};
            ComponentBodyInstance body{
                0,
                {visual.x + visual.width / 2 - bounds.centerX() * size.x,
                 visual.y + visual.height / 2 - bounds.centerY() * size.y},
                size,
                definition->presentation.shader.key,
                definition->presentation.body
            };
            if (!m_canCreate)
                body.style.tint[3] *= 0.45f;
            renderer.drawScreenComponent(body);
            if (!definition->presentation.bodyLabel.empty())
                label(
                    definition->presentation.bodyLabel,
                    {body.position.x - body.size.x / 2,
                     body.position.y - body.size.y / 2,
                     body.size.x,
                     body.size.y},
                    0.32f,
                    color,
                    true
                );
            label(
                button.label,
                {rect.x, rect.y + rect.height - 26, rect.width, 24},
                0.45f,
                color,
                true
            );
        }
        else
        {
            label(button.label, {rect.x, rect.y + 3, rect.width, 22}, 0.48f, color);
            label(
                std::to_string(button.inputCount) + " inputs / " +
                    std::to_string(button.outputCount) + " outputs",
                {rect.x, rect.y + 25, rect.width, 18},
                0.35f,
                {0.55f, 0.65f, 0.76f, 1}
            );
        }
    }
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(std::nullopt);
    const auto footer = !m_message.empty()  ? m_message
                        : !m_canCreate      ? "F2: switch to Selection"
                        : m_buttons.empty() ? "No custom components"
                        : m_maxScroll > 0   ? "Scroll for more components"
                                            : "Release on the canvas";
    label(
        footer,
        {6, m_panel.y + m_panel.height - 36, m_panel.width - 12, 22},
        0.35f,
        m_message.empty() ? glm::vec4{0.55f, 0.65f, 0.76f, 1} : glm::vec4{1, 0.55f, 0.4f, 1}
    );
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(m_bottom);
    renderer.drawScreenRect(m_bottom, {0.075f, 0.09f, 0.12f, 1});
    renderer.drawScreenRect({m_bottom.x, m_bottom.y, m_bottom.width, 1}, {0.18f, 0.23f, 0.3f, 1});
    for (const auto tab : {BottomTab::First, BottomTab::Second})
    {
        const auto bounds = bottomTabBounds(tab);
        const bool active = tab == m_bottomTab;
        renderer.drawScreenRect(
            bounds, active ? glm::vec4{0.16f, 0.3f, 0.46f, 1} : glm::vec4{0.11f, 0.15f, 0.21f, 1}
        );
        if (active)
            renderer.drawScreenRect(
                {bounds.x, bounds.y + bounds.height - 2, bounds.width, 2}, {0.3f, 0.65f, 0.95f, 1}
            );
        label(tab == BottomTab::First ? "Tab 1" : "Tab 2", bounds, 0.42f, ink, true);
    }
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(std::nullopt);
    if (m_bottomTab == BottomTab::First && m_circuitTabs.enabled())
        label(
            m_circuitTabs.activeViewLabel(),
            {m_bottom.x + 8, m_bottom.y + 42, std::max(0.0, m_bottom.width - 16), 26},
            0.45f,
            ink
        );
    if (dragging())
    {
        const auto* definition = scene.getComponentCatalog().find(m_dragDefinition);
        if (const auto position = dropPosition(camera))
        {
            const auto origin = GridSystem::gridToWorld(*position);
            const auto bounds = bodyBounds(
                definition->presentation.body,
                origin.x,
                origin.y,
                definition->layout.width,
                definition->layout.height
            );
            renderer.drawComponentBoundingBox(bounds, 0.01f, 0.8f);
        }
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
        const bool named = canName(scene);
        if (named)
        {
            const auto field = nameBounds(scene);
            label(
                "Name",
                {field.x - 10, field.y - 18, field.width + 20, 18},
                0.35f,
                {0.55f, 0.65f, 0.76f, 1}
            );
            renderer.drawScreenRect(
                field,
                m_nameEditing ? glm::vec4{0.3f, 0.65f, 0.95f, 1} : glm::vec4{0.25f, 0.35f, 0.46f, 1}
            );
            renderer.drawScreenRect(
                {field.x + 1, field.y + 1, field.width - 2, field.height - 2},
                {0.06f, 0.09f, 0.13f, 1}
            );
            const auto& name = scene.getCommittedComponentView(m_infoComponent)->getBodyLabel();
            label(
                m_nameEditing ? m_nameDraft + "|" : (name.empty() ? "(unnamed)" : name),
                field,
                0.43f,
                ink
            );
        }
        const int rows = infoVisibleRows(scene);
        const int offset =
            std::clamp(m_infoScroll, 0, std::max(0, static_cast<int>(info.size()) - 1 - rows));
        for (int row = 0; row < rows && row + offset + 1 < static_cast<int>(info.size()); ++row)
            label(
                info[row + offset + 1],
                {bounds.x + 4,
                 bounds.y + infoHeaderHeight + (named ? infoNameHeight : 0) + row * infoRowHeight,
                 bounds.width - 8,
                 infoRowHeight},
                0.43f,
                ink
            );
        label(
            !m_nameError.empty()                       ? m_nameError
            : m_nameEditing                            ? "Enter: save; Esc: cancel"
            : named && m_canCreate                     ? "Click name to edit; Esc: close"
            : named                                    ? "F2: Selection to rename"
            : static_cast<int>(info.size()) - 1 > rows ? "Scroll; Esc / outside to close"
                                                       : "Esc / click outside to close",
            {bounds.x, bounds.y + bounds.height - infoFooterHeight, bounds.width, infoFooterHeight},
            0.33f,
            {0.55f, 0.65f, 0.76f, 1}
        );
    }
    renderer.drawText(text, TextSpace::Screen);
    m_circuitTabs.draw(renderer);
    text.clear();
    renderer.drawScreenRect(m_bar, {0.075f, 0.09f, 0.12f, 1});
    renderer.drawScreenRect({0, m_bar.height - 1, m_bar.width, 1}, {0.18f, 0.23f, 0.3f, 1});
    if (m_fileMenuOpen || fileBounds().contains(m_pointer.x, m_pointer.y))
        renderer.drawScreenRect(fileBounds(), {0.16f, 0.3f, 0.46f, 1});
    label("File", fileBounds(), 0.42f, ink, true);
    if (!m_fileStatus.empty())
        label(
            m_fileStatus,
            {76, 0, std::max(0.0, m_bar.width - 80), m_bar.height},
            0.35f,
            m_fileError ? glm::vec4{1, 0.55f, 0.4f, 1} : glm::vec4{0.55f, 0.75f, 0.9f, 1}
        );
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    if (m_fileMenuOpen)
    {
        const auto menu = fileMenuBounds();
        renderer.setScreenClip(menu);
        renderer.drawScreenRect(menu, {0.3f, 0.45f, 0.6f, 1});
        renderer.drawScreenRect(
            {menu.x + 1, menu.y + 1, menu.width - 2, menu.height - 2}, {0.08f, 0.12f, 0.17f, 1}
        );
        int index = 0;
        for (const auto* name : {"Save", "Save As", "Open"})
        {
            const auto bounds = fileOptionBounds(index++);
            if (bounds.contains(m_pointer.x, m_pointer.y))
                renderer.drawScreenRect(bounds, {0.16f, 0.3f, 0.46f, 1});
            label(name, bounds, 0.42f, ink);
        }
        renderer.drawText(text, TextSpace::Screen);
        renderer.setScreenClip(std::nullopt);
    }
}
