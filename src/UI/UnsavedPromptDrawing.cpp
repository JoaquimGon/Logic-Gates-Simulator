#include "Graphics/Renderer.h"
#include "UI/UI.h"

#include <algorithm>


/** @brief Draw the modal unsaved-work explanation and its explicit save, discard and cancel actions. */
void UI::drawUnsavedPrompt(Renderer& renderer) const
{
    if (!unsavedPromptOpen())
        return;
    renderer.setScreenClip(std::nullopt);
    renderer.drawScreenRect({0, 0, double(m_surface.windowWidth), double(m_surface.windowHeight)}, {0, 0, 0, 0.5f});
    const auto box = unsavedPromptBounds();
    renderer.drawScreenRect(box, {0.2f, 0.35f, 0.5f, 1});
    renderer.drawScreenRect({box.x + 1, box.y + 1, box.width - 2, box.height - 2}, {0.08f, 0.11f, 0.15f, 1});
    std::vector<TextRun> text;
    auto line = [&](std::string value, CanvasViewport bounds, float scale)
    {
        const auto& font = renderer.fontMetrics();
        const auto width = getTextWidth(value, 1, font);
        if (width > 0)
            scale = std::min(scale, float(std::max(0.0, bounds.width - 16)) / width);
        text.push_back(
            {value,
             {float(bounds.x + 8), float(bounds.y + bounds.height / 2) + getCapHeight(scale, font) / 2},
             scale,
             {0.87f, 0.91f, 0.97f, 1}}
        );
    };
    renderer.setScreenClip(box);
    line("Save unsaved changes?", {box.x + 8, box.y + 8, box.width - 16, 30}, 0.52f);
    line(
        m_unsavedExiting ? "Before exiting:" : "Before closing or replacing this circuit:",
        {box.x + 8, box.y + 40, box.width - 16, 24}, 0.38f
    );
    for (int i = 0; i < 4 && i + m_unsavedScroll < int(m_unsavedNames.size()); ++i)
        line(m_unsavedNames[i + m_unsavedScroll], {box.x + 12, box.y + 68 + i * 22, box.width - 24, 22}, 0.4f);
    if (m_unsavedNames.size() > 4)
        line("Scroll for more unsaved circuits", {box.x + 8, box.y + 158, box.width - 16, 24}, 0.32f);
    for (const auto choice : {UnsavedChoice::Save, UnsavedChoice::Discard, UnsavedChoice::Cancel})
    {
        const auto bounds = unsavedButtonBounds(choice);
        renderer.drawScreenRect(bounds, {0.16f, 0.3f, 0.46f, 1});
        line(
            choice == UnsavedChoice::Save      ? (m_unsavedExiting ? "Save All" : "Save")
            : choice == UnsavedChoice::Discard ? (m_unsavedExiting ? "Discard All" : "Discard")
                                               : "Cancel",
            bounds, 0.4f
        );
    }
    renderer.drawText(text, TextSpace::Screen);
    renderer.setScreenClip(std::nullopt);
}
