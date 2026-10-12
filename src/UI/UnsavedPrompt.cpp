#include "Editor/Input.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

void UI::showUnsavedPrompt(std::vector<std::string> names, bool exiting, Input& input)
{
    dismissPopups(input);
    input.cancelCurrentAction();
    m_unsavedNames = std::move(names);
    m_unsavedExiting = exiting;
    m_unsavedScroll = 0;
    m_unsavedChoice.reset();
    m_unsavedPressed.reset();
    input.setUiCapture({true, true});
}

std::optional<UI::UnsavedChoice> UI::takeUnsavedChoice()
{
    return std::exchange(m_unsavedChoice, std::nullopt);
}

CanvasViewport UI::unsavedPromptBounds() const
{
    const double width = std::min(480.0, std::max(0.0, m_surface.windowWidth - 16.0));
    const double height = std::min(238.0, std::max(0.0, m_surface.windowHeight - 16.0));
    return {
        (m_surface.windowWidth - width) / 2, (m_surface.windowHeight - height) / 2, width, height
    };
}

CanvasViewport UI::unsavedButtonBounds(UnsavedChoice choice) const
{
    const auto popup = unsavedPromptBounds();
    const double width = std::max(0.0, (popup.width - 32) / 3);
    return {
        popup.x + 8 + static_cast<int>(choice) * (width + 8), popup.y + popup.height - 40, width, 30
    };
}

bool UI::handleUnsavedPrompt(const UiInputEvent& event, Input& input)
{
    if (m_unsavedChoice)
        return true; // Do not let queued input mutate the pending operation's target.
    input.setUiCapture({true, true});
    if (event.kind == UiInputKind::Scroll && std::isfinite(event.y))
        m_unsavedScroll = static_cast<int>(std::clamp(
            m_unsavedScroll - event.y,
            0.0,
            static_cast<double>(std::max(0, int(m_unsavedNames.size()) - 4))
        ));
    std::optional<UnsavedChoice> choice;
    if (event.kind == UiInputKind::Key && event.code == GLFW_KEY_ESCAPE &&
        event.action == GLFW_PRESS)
        choice = UnsavedChoice::Cancel;
    if (event.kind == UiInputKind::WindowFocus && !event.focused)
        m_unsavedPressed.reset();
    if (event.kind == UiInputKind::MouseButton && event.code == GLFW_MOUSE_BUTTON_LEFT)
    {
        for (const auto button :
             {UnsavedChoice::Save, UnsavedChoice::Discard, UnsavedChoice::Cancel})
            if (unsavedButtonBounds(button).contains(event.x, event.y))
            {
                if (event.action == GLFW_PRESS)
                    m_unsavedPressed = button;
                else if (event.action == GLFW_RELEASE && m_unsavedPressed == button)
                    choice = button;
            }
        if (event.action == GLFW_RELEASE)
            m_unsavedPressed.reset();
    }
    if (choice)
    {
        m_unsavedNames.clear();
        m_unsavedChoice = choice;
    }
    return true;
}
