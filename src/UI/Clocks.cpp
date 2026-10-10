#include "Editor/Actions/EditorActions.h"
#include "Editor/Input.h"
#include "Editor/Scene.h"
#include "UI/UI.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

std::string UI::frequencyText(float frequency)
{
    std::ostringstream text;
    text << std::setprecision(5) << frequency;
    return text.str();
}

ClockTimeline& UI::clockTimeline()
{
    return m_views ? m_views->activeClockTimeline() : m_clockTimeline;
}

const ClockTimeline& UI::clockTimeline() const
{
    return m_views ? m_views->activeClockTimeline() : m_clockTimeline;
}

CanvasViewport UI::clockControlBounds(ClockControl control) const
{
    constexpr double widths[] = {66, 68, 72, 26, 26, 44};
    const int index = static_cast<int>(control);
    const double scale = std::min(1.0, std::max(0.0, m_bottom.width - 16) / 332.0);
    double x = m_bottom.x + 8;
    for (int i = 0; i < index; ++i)
        x += (widths[i] + 6) * scale;
    return {x, m_bottom.y + 40, widths[index] * scale, 24};
}

CanvasViewport UI::clockListBounds() const
{
    return {
        m_bottom.x + 8,
        m_bottom.y + 88,
        std::max(0.0, m_bottom.width - 16),
        std::max(0.0, m_bottom.height - 96)
    };
}

double UI::clockLabelWidth() const
{
    return std::min(
        220.0,
        std::min(clockListBounds().width * 0.65, std::max(160.0, clockListBounds().width * 0.4))
    );
}

CanvasViewport UI::clockFrequencyBounds(int row) const
{
    const auto list = clockListBounds();
    return {list.x + 2, list.y + row * 48 + 23, std::max(0.0, clockLabelWidth() - 100), 22};
}

CanvasViewport UI::clockPauseBounds(int row) const
{
    const auto frequency = clockFrequencyBounds(row);
    return {frequency.x + frequency.width + 6, frequency.y, 78, 22};
}

int UI::clockScroll(const Scene& scene) const
{
    const int rows = std::max(1, static_cast<int>(clockListBounds().height / 48));
    return std::clamp(
        m_clockScroll, 0, std::max(0, static_cast<int>(scene.clockReadings().size()) - rows)
    );
}

double UI::clockStartTime() const
{
    const double end = m_clockFollow
                           ? std::max(m_clockWindow, clockTimeline().time())
                           : std::min(m_clockEnd, std::max(m_clockWindow, clockTimeline().time()));
    return std::max(0.0, end - m_clockWindow);
}

void UI::closeClockEdit(Input& input)
{
    if (m_clockEditing != -1)
    {
        m_clockEditing = -1;
        m_clockDraft.clear();
        m_clockError.clear();
        input.setUiCapture({});
        input.setCanvasFocused(true);
    }
}

bool UI::handleClocks(const UiInputEvent& event, Scene& scene, Input& input)
{
    if (event.kind == UiInputKind::WindowFocus && !event.focused)
    {
        closeClockEdit(input);
        m_clockMousePressed = false;
        m_clockCommittedKey = -1;
        return false;
    }
    if (event.kind == UiInputKind::MouseButton && event.action == GLFW_RELEASE &&
        m_clockMousePressed)
    {
        m_clockMousePressed = false;
        if (m_clockEditing == -1)
            input.setCanvasFocused(true);
        return true;
    }
    if (event.kind == UiInputKind::Key && event.code == m_clockCommittedKey)
    {
        if (event.action == GLFW_RELEASE)
        {
            m_clockCommittedKey = -1;
            input.setCanvasFocused(true);
        }
        return true;
    }
    if (m_clockEditing != -1)
    {
        if (!scene.getLogicComponent(m_clockEditing) || m_bottomTab != BottomTab::Clocks)
            closeClockEdit(input);
        else if (event.kind == UiInputKind::Key)
        {
            if (event.action == GLFW_PRESS || event.action == GLFW_REPEAT)
            {
                if (event.code == GLFW_KEY_ESCAPE)
                {
                    closeClockEdit(input);
                    m_clockCommittedKey = event.code;
                }
                else if (event.code == GLFW_KEY_BACKSPACE)
                {
                    if (m_clockReplaceDraft)
                        m_clockDraft.clear();
                    else if (!m_clockDraft.empty())
                        m_clockDraft.pop_back();
                    m_clockReplaceDraft = false;
                }
                else if (event.code == GLFW_KEY_ENTER || event.code == GLFW_KEY_KP_ENTER)
                {
                    float hz = 0;
                    const auto parsed = std::from_chars(
                        m_clockDraft.data(), m_clockDraft.data() + m_clockDraft.size(), hz
                    );
                    if (parsed.ec != std::errc{} ||
                        parsed.ptr != m_clockDraft.data() + m_clockDraft.size() ||
                        !std::isfinite(hz) || hz < 0.1f)
                        m_clockError = "Enter a frequency of at least 0.1 Hz.";
                    else
                    {
                        const auto result =
                            EditorActions(scene).apply({ConfigureComponentProperties{
                                .componentId = m_clockEditing, .clockFrequency = hz
                            }});
                        input.recordEdit(result);
                        if (result)
                        {
                            closeClockEdit(input);
                            m_clockCommittedKey = event.code;
                        }
                        else
                            m_clockError = result.message;
                    }
                }
            }
            return true;
        }
        else if (event.kind == UiInputKind::Text)
        {
            if ((event.codepoint >= '0' && event.codepoint <= '9') || event.codepoint == '.' ||
                event.codepoint == 'e' || event.codepoint == 'E' || event.codepoint == '+' ||
                event.codepoint == '-')
            {
                if (m_clockReplaceDraft)
                    m_clockDraft.clear();
                m_clockReplaceDraft = false;
                if (m_clockDraft.size() < 16)
                    m_clockDraft.push_back(static_cast<char>(event.codepoint));
            }
            return true;
        }
        else if (event.kind == UiInputKind::MouseButton && event.action == GLFW_PRESS)
            closeClockEdit(input);
    }
    if (m_bottomTab != BottomTab::Clocks || dragging() || m_fileMenuOpen ||
        m_circuitTabs.popupOpen() || m_infoComponent != -1)
        return false;
    const auto list = clockListBounds();
    if (event.kind == UiInputKind::Scroll && m_bottom.contains(m_pointer.x, m_pointer.y) &&
        std::isfinite(event.y))
    {
        closeClockEdit(input);
        if ((event.modifiers & GLFW_MOD_SHIFT) || event.x != 0)
        {
            m_clockEnd = std::clamp(
                clockStartTime() + m_clockWindow -
                    (event.x != 0 ? event.x : event.y) * m_clockWindow * 0.1,
                m_clockWindow,
                std::max(m_clockWindow, clockTimeline().time())
            );
            m_clockFollow = false;
        }
        else
            m_clockScroll = static_cast<int>(std::clamp(
                clockScroll(scene) - event.y,
                0.0,
                static_cast<double>(std::max(0, static_cast<int>(scene.clockReadings().size()) - 1))
            ));
        return true;
    }
    if (event.kind != UiInputKind::MouseButton || event.action != GLFW_PRESS ||
        event.code != GLFW_MOUSE_BUTTON_LEFT || !m_bottom.contains(event.x, event.y))
        return false;
    // Bottom-tab selection remains in the existing UI route.
    if (event.y < m_bottom.y + 36)
        return false;
    m_clockMousePressed = true;
    input.setCanvasFocused(false);
    for (auto control :
         {ClockControl::RunPause,
          ClockControl::SingleStep,
          ClockControl::DoubleStep,
          ClockControl::ZoomOut,
          ClockControl::ZoomIn,
          ClockControl::Live})
        if (clockControlBounds(control).contains(event.x, event.y))
        {
            closeClockEdit(input);
            if (control == ClockControl::RunPause)
                scene.togglePauseAllClocks();
            else if (control == ClockControl::SingleStep || control == ClockControl::DoubleStep)
            {
                const int count = control == ClockControl::SingleStep ? 1 : 2;
                const int stepped = scene.stepAllClocks(count, &clockTimeline());
                m_clockFollow = true;
                m_clockError = stepped < count && scene.getLastEvalResult() == SimulationResult::OK
                                   ? "Enable a clock to step."
                                   : "";
            }
            else if (control == ClockControl::Live)
                m_clockFollow = true;
            else
            {
                m_clockEnd = clockStartTime() + m_clockWindow;
                m_clockWindow = std::clamp(
                    m_clockWindow * (control == ClockControl::ZoomIn ? 0.5 : 2.0), 0.000001, 3600.0
                );
            }
            clockTimeline().record(scene.clockTime(), scene.clockReadings());
            return true;
        }
    if (!list.contains(event.x, event.y))
        return true;
    const auto readings = scene.clockReadings();
    const int offset = clockScroll(scene);
    for (int row = 0; row + offset < static_cast<int>(readings.size()) && row * 48 < list.height;
         ++row)
    {
        const auto& clock = readings[row + offset];
        if (clock.path.size() != 1)
            continue; // Configure nested clocks in their subcircuit's editor.
        if (clockFrequencyBounds(row).contains(event.x, event.y))
        {
            m_clockEditing = clock.path.front();
            m_clockDraft = frequencyText(clock.frequency);
            m_clockReplaceDraft = true;
            m_clockError.clear();
            input.setUiCapture({true, false});
        }
        else if (clockPauseBounds(row).contains(event.x, event.y))
        {
            const auto result = EditorActions(scene).apply({ConfigureComponentProperties{
                .componentId = clock.path.front(), .clockPaused = !clock.paused
            }});
            input.recordEdit(result);
            m_clockError = result ? "" : result.message;
        }
    }
    return true;
}
