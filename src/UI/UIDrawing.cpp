#include "Components/Gate.h"
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
                        : m_routingError    ? "Route blocked; Alt-drag / retry"
                        : !m_canCreate      ? "F2: switch to Selection"
                        : m_buttons.empty() ? "No custom components"
                        : m_maxScroll > 0   ? "Scroll for more components"
                                            : "Release on the canvas";
    label(
        footer,
        {6, m_panel.y + m_panel.height - 36, m_panel.width - 12, 22},
        0.35f,
        m_message.empty() && !m_routingError ? glm::vec4{0.55f, 0.65f, 0.76f, 1}
                                             : glm::vec4{1, 0.55f, 0.4f, 1}
    );
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(m_modes);
    for (const auto mode : {EditorMode::Selection, EditorMode::Interaction})
    {
        const auto bounds = modeButtonBounds(mode);
        if (bounds.width <= 0 || bounds.height <= 0)
            continue;
        const bool active = m_canCreate == (mode == EditorMode::Selection);
        renderer.drawScreenRect(
            bounds,
            active                  ? glm::vec4{0.11f, 0.15f, 0.21f, 1}
            : hoveredMode() == mode ? glm::vec4{0.2f, 0.4f, 0.6f, 1}
                                    : glm::vec4{0.16f, 0.3f, 0.46f, 1}
        );
        if (active)
            renderer.drawScreenRect(
                {bounds.x, bounds.y + bounds.height - 2, bounds.width, 2},
                {0.3f, 0.65f, 0.95f, 0.5f}
            );
        label(
            mode == EditorMode::Selection ? "Sel." : "Int.",
            bounds,
            0.42f,
            active                  ? glm::vec4{0.5f, 0.57f, 0.65f, 1}
            : hoveredMode() == mode ? glm::vec4{0.5f, 0.78f, 1, 1}
                                    : ink,
            true
        );
    }
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(m_bottom);
    renderer.drawScreenRect(m_bottom, {0.075f, 0.09f, 0.12f, 1});
    renderer.drawScreenRect({m_bottom.x, m_bottom.y, m_bottom.width, 1}, {0.18f, 0.23f, 0.3f, 1});
    for (const auto tab : {BottomTab::Subcircuit, BottomTab::Messages})
    {
        if (tab == BottomTab::Subcircuit && !m_showSubcircuit)
            continue;
        const auto bounds = bottomTabBounds(tab);
        const bool active = tab == m_bottomTab;
        renderer.drawScreenRect(
            bounds, active ? glm::vec4{0.16f, 0.3f, 0.46f, 1} : glm::vec4{0.11f, 0.15f, 0.21f, 1}
        );
        if (active)
            renderer.drawScreenRect(
                {bounds.x, bounds.y + bounds.height - 2, bounds.width, 2}, {0.3f, 0.65f, 0.95f, 1}
            );
        label(
            tab == BottomTab::Subcircuit ? "Subcircuit"
            : hasUnreadMessages()        ? "Messages *"
                                         : "Messages",
            bounds,
            0.42f,
            ink,
            true
        );
    }
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    if (m_bottomTab == BottomTab::Subcircuit && m_showSubcircuit)
    {
        const auto info = subcircuitInfo(scene);
        for (int row = 0; row < 3; ++row)
            label(
                info[row],
                {m_bottom.x + 8,
                 m_bottom.y + 40 + row * 22,
                 std::max(0.0, m_bottom.width - 16),
                 22},
                0.4f,
                row == 1 ? (info[row].starts_with("Valid") ? glm::vec4{0.4f, 0.85f, 0.6f, 1}
                                                           : glm::vec4{1, 0.55f, 0.4f, 1})
                         : ink
            );
        renderer.drawText(text, TextSpace::Screen);
        text.clear();
        const auto list = subcircuitListBounds();
        renderer.setScreenClip(list);
        const int rows = static_cast<int>(list.height / 22);
        const int offset = std::clamp(
            m_subcircuitScroll, 0, std::max(0, static_cast<int>(info.size()) - 3 - rows)
        );
        for (int row = 0; row < rows && row + offset + 3 < static_cast<int>(info.size()); ++row)
            label(info[row + offset + 3], {list.x, list.y + row * 22, list.width, 22}, 0.4f, ink);
        renderer.drawText(text, TextSpace::Screen);
        text.clear();
    }
    if (m_bottomTab == BottomTab::Messages)
    {
        label(
            "Current circuit problems",
            {m_bottom.x + 8, m_bottom.y + 38, std::max(0.0, m_bottom.width - 16), 20},
            0.32f,
            {0.55f, 0.65f, 0.76f, 1}
        );
        renderer.drawText(text, TextSpace::Screen);
        text.clear();
        const auto box = messageBoxBounds();
        renderer.drawScreenRect(box, {0.045f, 0.06f, 0.08f, 1});
        renderer.setScreenClip(box);
        const auto lines = messageRows(&renderer.fontMetrics());
        const int rows = static_cast<int>(box.height / 20);
        const int offset = std::clamp(
            messageState().scroll, 0, std::max(0, static_cast<int>(lines.size()) - rows)
        );
        if (lines.empty())
            label("Circuit is fine.", {box.x, box.y, box.width, 20}, 0.4f, {0.4f, 0.85f, 0.6f, 1});
        for (int row = 0; row < rows && row + offset < static_cast<int>(lines.size()); ++row)
        {
            const auto& line = lines[row + offset];
            const auto color = line.kind == MessageKind::Error ? glm::vec4{1, 0.55f, 0.4f, 1}
                               : line.kind == MessageKind::Warning
                                   ? glm::vec4{0.9f, 0.72f, 0.38f, 1}
                                   : ink;
            const bool find = line.issue && simulationIssues()[*line.issue].location;
            label(
                line.text,
                {box.x, box.y + row * 20, box.width - (find ? 60 : 0), 20},
                0.4f,
                line.text == "Circuit is fine." ? glm::vec4{0.4f, 0.85f, 0.6f, 1} : color
            );
            if (find)
            {
                const auto target = findMessageBounds(*line.issue);
                text.push_back(
                    {"FIND",
                     {static_cast<float>(target.x),
                      static_cast<float>(target.y + target.height / 2) +
                          getCapHeight(0.35f, renderer.fontMetrics()) / 2},
                     0.35f,
                     {0.4f, 0.75f, 1, 1}}
                );
                renderer.drawScreenRect(
                    {target.x, target.y + target.height - 2, target.width, 1}, {0.4f, 0.75f, 1, 1}
                );
            }
        }
        renderer.drawText(text, TextSpace::Screen);
        text.clear();
    }
    renderer.setScreenClip(std::nullopt);
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
                infoGate(scene) ? "Label" : "Name",
                {field.x - 10, field.y - 18, field.width + 20, 18},
                0.35f,
                {0.55f, 0.65f, 0.76f, 1}
            );
            renderer.drawScreenRect(
                field,
                m_nameEditing  ? glm::vec4{0.3f, 0.65f, 0.95f, 1}
                : !m_canCreate ? glm::vec4{0.14f, 0.2f, 0.27f, 1}
                               : glm::vec4{0.25f, 0.35f, 0.46f, 1}
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
                m_canCreate ? ink : glm::vec4{0.45f, 0.52f, 0.6f, 1}
            );
        }
        const auto* gate = infoGate(scene);
        if (gate)
        {
            const auto settings = gateSettingsBounds(scene);
            const bool editable = m_canCreate && !m_nameEditing;
            const auto muted = glm::vec4{0.45f, 0.52f, 0.6f, 1};
            label("Inputs", {settings.x, settings.y, settings.width - 100, 26}, 0.43f, ink);
            label(
                std::to_string(gate->getInputPinCount()) + (canResizeGate(scene) ? "" : " (fixed)"),
                {settings.x + settings.width - (canResizeGate(scene) ? 64 : 100),
                 settings.y,
                 canResizeGate(scene) ? 36.0 : 100.0,
                 26},
                0.43f,
                ink,
                true
            );
            auto button = [&](CanvasViewport rect, const std::string& value, bool enabled)
            {
                renderer.drawScreenRect(
                    rect,
                    !enabled                                  ? glm::vec4{0.1f, 0.14f, 0.19f, 1}
                    : rect.contains(m_pointer.x, m_pointer.y) ? glm::vec4{0.16f, 0.3f, 0.46f, 1}
                                                              : glm::vec4{0.12f, 0.2f, 0.29f, 1}
                );
                label(value, rect, 0.43f, enabled ? ink : muted, true);
            };
            if (canResizeGate(scene))
                for (bool increase : {false, true})
                    button(
                        gateInputButtonBounds(scene, increase),
                        increase ? "+" : "-",
                        editable &&
                            (increase ? gate->getInputPinCount() < 8 : gate->getInputPinCount() > 2)
                    );
            const std::string inversion =
                std::string("Inverted: ") + (gate->isInverted() ? "On" : "Off");
            if (canInvertGate(scene))
                button(gateInversionBounds(scene), inversion, editable);
            else
                label(
                    inversion + " (fixed)",
                    {settings.x, settings.y + 30, settings.width, 26},
                    0.43f,
                    muted
                );
        }
        const int rows = infoVisibleRows(scene);
        const int offset =
            std::clamp(m_infoScroll, 0, std::max(0, static_cast<int>(info.size()) - 1 - rows));
        for (int row = 0; row < rows && row + offset + 1 < static_cast<int>(info.size()); ++row)
            label(
                info[row + offset + 1],
                {bounds.x + 4,
                 bounds.y + infoHeaderHeight + (named ? infoNameHeight : 0) +
                     (gate ? infoGateHeight : 0) + row * infoRowHeight,
                 bounds.width - 8,
                 infoRowHeight},
                0.43f,
                ink
            );
        if (canEditSubcircuit(scene))
        {
            const auto edit = editSubcircuitBounds(scene);
            renderer.drawScreenRect(
                edit,
                edit.contains(m_pointer.x, m_pointer.y) ? glm::vec4{0.16f, 0.3f, 0.46f, 1}
                                                        : glm::vec4{0.12f, 0.2f, 0.29f, 1}
            );
            label("Edit subcircuit", edit, 0.4f, ink, true);
        }
        label(
            !m_infoError.empty()                       ? m_infoError
            : m_nameEditing                            ? "Enter: save; Esc: cancel"
            : gate && m_canCreate                      ? "Edit settings; Esc: close"
            : gate                                     ? "F2: Selection to edit settings"
            : named && m_canCreate                     ? "Click name to edit; Esc: close"
            : named                                    ? "F2: Selection to rename"
            : static_cast<int>(info.size()) - 1 > rows ? "Scroll; Esc / outside to close"
                                                       : "Esc / click outside to close",
            {bounds.x, bounds.y + bounds.height - infoFooterHeight, bounds.width, infoFooterHeight},
            0.33f,
            m_infoError.empty() ? glm::vec4{0.55f, 0.65f, 0.76f, 1} : glm::vec4{1, 0.55f, 0.4f, 1}
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
    const std::string unsaved = !m_circuitTabs.activeHasUnsavedChanges() ? ""
                                : m_circuitTabs.activeIsSubcircuit()
                                    ? "Unsaved changes - save for Custom"
                                    : "Unsaved changes";
    const double unsavedWidth =
        unsaved.empty()
            ? 0
            : std::min(
                  std::max(0.0, m_bar.width - 76),
                  static_cast<double>(getTextWidth(unsaved, 0.35f, renderer.fontMetrics())) + 20
              );
    if (!unsaved.empty())
        label(
            unsaved,
            {m_bar.width - unsavedWidth, 0, unsavedWidth, m_bar.height},
            0.35f,
            {0.9f, 0.72f, 0.38f, 1}
        );
    const auto& log = messageState();
    const std::string status = log.simulationResult != SimulationResult::OK
                                   ? "Simulation paused - see Messages"
                               : log.shortedNets > 0 ? "Short circuit - see Messages"
                               : log.behind          ? "Simulation behind real time - see Messages"
                                                     : log.fileStatus;
    if (!status.empty())
        label(
            status,
            {76, 0, std::max(0.0, m_bar.width - unsavedWidth - 80), m_bar.height},
            0.35f,
            log.simulationResult != SimulationResult::OK || log.shortedNets > 0 ||
                    (!log.behind && log.fileError)
                ? glm::vec4{1, 0.55f, 0.4f, 1}
            : log.behind ? glm::vec4{0.9f, 0.72f, 0.38f, 1}
                         : glm::vec4{0.55f, 0.75f, 0.9f, 1}
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
        for (const auto& name : fileOptions())
        {
            const auto bounds = fileOptionBounds(index++);
            if (bounds.contains(m_pointer.x, m_pointer.y))
                renderer.drawScreenRect(bounds, {0.16f, 0.3f, 0.46f, 1});
            label(name, bounds, 0.42f, ink);
        }
        renderer.drawText(text, TextSpace::Screen);
        renderer.setScreenClip(std::nullopt);
    }
    if (const auto mode = hoveredMode())
    {
        const auto bounds = modeTooltipBounds();
        renderer.drawScreenRect(bounds, {0.3f, 0.45f, 0.6f, 1});
        renderer.drawScreenRect(
            {bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2},
            {0.08f, 0.12f, 0.17f, 1}
        );
        const bool selection = *mode == EditorMode::Selection;
        const bool active = m_canCreate == selection;
        label(
            std::string(editorModeName(*mode)) + " mode" + (active ? " (active)" : ""),
            {bounds.x, bounds.y + 4, bounds.width, 24},
            0.48f,
            ink
        );
        label(
            selection ? "Move, select, edit and wire components."
                      : "Toggle inputs and operate clock controls.",
            {bounds.x, bounds.y + 30, bounds.width, 20},
            0.38f,
            ink
        );
        label(
            selection ? "Inputs stay unchanged when moved." : "Components stay in place.",
            {bounds.x, bounds.y + 50, bounds.width, 20},
            0.38f,
            ink
        );
        label(
            "F2 also switches modes.",
            {bounds.x, bounds.y + 74, bounds.width, 18},
            0.33f,
            {0.55f, 0.65f, 0.76f, 1}
        );
        renderer.drawText(text, TextSpace::Screen);
    }
}
