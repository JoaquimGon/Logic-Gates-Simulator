#include "Editor/Scene.h"
#include "Graphics/Renderer.h"
#include "UI/UI.h"

#include <algorithm>

void UI::drawTruthTable(Renderer& renderer, const Scene& scene) const
{
    const auto& table = truthTable(scene);
    const auto popup = infoBounds(scene);
    const auto area = truthTableRowsBounds(scene);
    const auto current = truthTableCurrentRow(scene);
    const auto ink = glm::vec4{0.87f, 0.91f, 0.97f, 1};
    const auto muted = glm::vec4{0.55f, 0.65f, 0.76f, 1};
    std::vector<TextRun> text;
    auto label = [&](const std::string& value,
                     CanvasViewport rect,
                     float scale,
                     glm::vec4 color,
                     bool centered = false)
    {
        const auto& font = renderer.fontMetrics();
        const float width = getTextWidth(value, 1, font);
        if (width > 0)
            scale = std::min(scale, static_cast<float>(std::max(0.0, rect.width - 8)) / width);
        text.push_back(
            {value,
             {static_cast<float>(centered ? rect.x + (rect.width - width * scale) / 2 : rect.x + 4),
              static_cast<float>(rect.y + rect.height / 2) + getCapHeight(scale, font) / 2},
             scale,
             color}
        );
    };
    renderer.setScreenClip(
        CanvasViewport{
            popup.x + 1,
            popup.y + infoHeaderSize(scene),
            popup.width - 2,
            std::max(0.0, popup.height - infoHeaderSize(scene) - infoFooterHeight)
        }
    );
    double y = popup.y + infoHeaderSize(scene);
    for (const auto& note : table.notes)
    {
        label(note, {popup.x + 6, y, popup.width - 12, 20}, 0.35f, muted);
        y += 20;
    }
    const auto* view = scene.getCommittedComponentView(m_infoComponent);
    auto states = [&](const std::vector<PinUI>& pins, const char* prefix)
    {
        std::string result = prefix;
        for (const auto& pin : pins)
        {
            if (result.back() != ' ')
                result += "  ";
            result +=
                (pin.label.empty() ? "Pin" + std::to_string(pin.pin_index + 1) : pin.label) + "=" +
                (scene.isSimulationDirty() || scene.getLastEvalResult() != SimulationResult::OK ||
                         pin.state == PinState::DISCONNECTED
                     ? "?"
                 : pin.state == PinState::ON ? "1"
                                             : "0");
        }
        return result;
    };
    label(
        states(view->getInputPins(), "Now: "), {popup.x + 6, y, popup.width - 12, 22}, 0.38f, ink
    );
    label(
        states(view->getOutputPins(), "Outputs: "),
        {popup.x + 6, y + 22, popup.width - 12, 22},
        0.38f,
        ink
    );
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    if (!table.rows.empty())
    {
        const double columnWidth = (area.width - 8) / table.headers.size();
        const CanvasViewport headings{area.x, area.y - infoRowHeight, area.width, infoRowHeight};
        renderer.drawScreenRect(headings, {0.12f, 0.2f, 0.29f, 1});
        for (std::size_t column = 0; column < table.headers.size(); ++column)
        {
            const auto& name = table.headers[column];
            label(
                name.size() > 10 ? name.substr(0, 7) + "..." : name,
                {area.x + column * columnWidth, headings.y, columnWidth, infoRowHeight},
                0.38f,
                ink,
                true
            );
        }
        renderer.drawText(text, TextSpace::Screen);
        text.clear();
        renderer.setScreenClip(area);
        const int offset = truthTableScroll(scene);
        const int visible = truthTableVisibleRows(scene);
        for (int row = 0; row < visible && row + offset < static_cast<int>(table.rows.size());
             ++row)
        {
            const CanvasViewport rect{
                area.x, area.y + row * infoRowHeight, area.width, infoRowHeight
            };
            const bool highlighted = current && row + offset == *current;
            renderer.drawScreenRect(
                rect,
                highlighted ? glm::vec4{0.12f, 0.32f, 0.24f, 1}
                : row % 2   ? glm::vec4{0.06f, 0.1f, 0.14f, 1}
                            : glm::vec4{0.045f, 0.075f, 0.11f, 1}
            );
            for (std::size_t column = 0; column < table.headers.size(); ++column)
                label(
                    table.rows[row + offset][column],
                    {rect.x + column * columnWidth, rect.y, columnWidth, infoRowHeight},
                    0.43f,
                    ink,
                    true
                );
        }
        if (static_cast<int>(table.rows.size()) > visible && visible > 0)
        {
            const double height = visible * infoRowHeight;
            const double thumb = std::max(10.0, height * visible / table.rows.size());
            renderer.drawScreenRect(
                {area.x + area.width - 5, area.y, 3, height}, {0.16f, 0.22f, 0.29f, 1}
            );
            renderer.drawScreenRect(
                {area.x + area.width - 5,
                 area.y + (height - thumb) * offset / (table.rows.size() - visible),
                 3,
                 thumb},
                {0.38f, 0.58f, 0.76f, 1}
            );
        }
        renderer.drawText(text, TextSpace::Screen);
        text.clear();
    }
    renderer.setScreenClip(std::nullopt);
    const auto button = currentTruthRowBounds(scene);
    if (button.width > 0)
    {
        renderer.drawScreenRect(
            button, current ? glm::vec4{0.12f, 0.2f, 0.29f, 1} : glm::vec4{0.1f, 0.14f, 0.19f, 1}
        );
        label("Current row", button, 0.35f, current ? ink : muted, true);
    }
    const int first = truthTableScroll(scene);
    const int last =
        std::min(first + truthTableVisibleRows(scene), static_cast<int>(table.rows.size()));
    std::string footer = !current ? "Inputs unavailable" : "Green = current inputs";
    if (table.rows.empty())
        footer = "Esc / click outside to close";
    else if (truthTableVisibleRows(scene) == 0)
        footer = "Enlarge window to see rows";
    else if (static_cast<int>(table.rows.size()) > truthTableVisibleRows(scene))
        footer = "Rows " + std::to_string(first + 1) + "-" + std::to_string(last) + " / " +
                 std::to_string(table.rows.size()) + " (scroll)";
    label(
        footer,
        {popup.x + 6,
         popup.y + popup.height - infoFooterHeight,
         popup.width - (button.width > 0 ? 112 : 12),
         infoFooterHeight},
        0.33f,
        muted
    );
    renderer.drawText(text, TextSpace::Screen);
}
