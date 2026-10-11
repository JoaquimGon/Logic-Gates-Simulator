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
    constexpr double widths[] = {66, 100, 100, 26, 26, 44, 40, 54};
    const int index = static_cast<int>(control);
    const double scale = std::min(1.0, std::max(0.0, m_bottom.width - 16) / 498.0);
    if (control == ClockControl::Clear)
        return {
            m_bottom.x + m_bottom.width - 8 - widths[index] * scale,
            m_bottom.y + 40,
            widths[index] * scale,
            24
        };
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
    const double window = clockWindowSeconds();
    const double end = m_clockFollow
                           ? std::max(window, clockTimeline().time())
                           : std::min(m_clockEnd, std::max(window, clockTimeline().time()));
    return std::max(0.0, end - window);
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
        m_clockHover = -1;
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
        m_clockHover = -1;
        if ((event.modifiers & GLFW_MOD_SHIFT) || event.x != 0)
        {
            const double window = clockWindowSeconds();
            m_clockEnd = std::clamp(
                clockStartTime() + window - (event.x != 0 ? event.x : event.y) * window * 0.1,
                window,
                std::max(window, clockTimeline().time())
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
    m_clockHover = -1;
    m_clockMousePressed = true;
    input.setCanvasFocused(false);
    for (auto control :
         {ClockControl::RunPause,
          ClockControl::SingleStep,
          ClockControl::DoubleStep,
          ClockControl::ZoomOut,
          ClockControl::ZoomIn,
          ClockControl::Live,
          ClockControl::Fit,
          ClockControl::Clear})
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
            else if (control == ClockControl::Fit)
            {
                m_clockWindow = fittedClockWindow(scene.clockReadings());
                m_clockAutoFit = m_clockFollow = true;
            }
            else if (control == ClockControl::Clear)
            {
                clockTimeline().clear();
                m_clockFollow = true;
                m_clockError.clear();
            }
            else
            {
                const double window = clockWindowSeconds();
                m_clockEnd = clockStartTime() + window;
                m_clockWindow = std::clamp(
                    window * (control == ClockControl::ZoomIn ? 0.5 : 2.0), 0.000001, 3600.0
                );
                m_clockAutoFit = false;
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

double UI::fittedClockWindow(const std::vector<ClockReading>& readings)
{
    double fastest = 0, disabled = 0;
    for (const auto& clock : readings)
    {
        if (clock.paused)
            disabled = std::max(disabled, static_cast<double>(clock.frequency));
        else
            fastest = std::max(fastest, static_cast<double>(clock.frequency));
    }
    if (fastest == 0)
        fastest = disabled;
    return fastest > 0 ? std::clamp(4.0 / fastest, 0.000001, 3600.0) : 2.0;
}

double UI::clockWindowSeconds() const
{
    return m_clockAutoFit && m_views ? fittedClockWindow(m_views->activeScene().clockReadings())
                                     : m_clockWindow;
}

bool UI::clockWaveformTooDense(float frequency) const
{
    const double graphWidth = clockListBounds().width - clockLabelWidth();
    // Two edges per cycle. Keep each half-cycle legible and bound the geometry per row.
    const double edgeLimit = std::min(128.0, graphWidth / 8.0);
    return 2.0 * frequency * clockWindowSeconds() > edgeLimit;
}

std::string UI::clockTimeFrameText() const
{
    std::ostringstream text;
    text << "Time frame = " << std::setprecision(6) << clockWindowSeconds() << " s";
    return text.str();
}

void UI::updateClockHover(double now)
{
    m_clockHoverNow = now;
    int target = -1;
    if (m_bottomTab == BottomTab::Clocks && !m_resizingBottom && !dragging() && !m_fileMenuOpen &&
        !m_circuitTabs.popupOpen() && m_infoComponent == -1 && m_clockEditing == -1)
    {
        for (int i = 0; i <= static_cast<int>(ClockControl::Clear); ++i)
            if (clockControlBounds(static_cast<ClockControl>(i)).contains(m_pointer.x, m_pointer.y))
                target = i;
        if (m_views && clockListBounds().contains(m_pointer.x, m_pointer.y))
        {
            const auto readings = m_views->activeScene().clockReadings();
            const int offset = clockScroll(m_views->activeScene());
            for (int row = 0; row + offset < static_cast<int>(readings.size()) &&
                              row * 48 < clockListBounds().height;
                 ++row)
                if (clockPauseBounds(row).contains(m_pointer.x, m_pointer.y))
                    target = 8 + row;
        }
    }
    if (target != m_clockHover || now < m_clockHoverSince)
    {
        m_clockHover = target;
        m_clockHoverSince = now;
    }
}

std::vector<std::string> UI::clockTooltip(const Scene& scene) const
{
    if (m_clockHover < 0 || m_clockHoverNow - m_clockHoverSince < 0.75 ||
        m_bottomTab != BottomTab::Clocks || m_fileMenuOpen || m_circuitTabs.popupOpen() ||
        m_infoComponent != -1 || m_resizingBottom || dragging())
        return {};
    const auto readings = scene.clockReadings();
    const bool enabled = std::any_of(
        readings.begin(), readings.end(), [](const auto& clock) { return !clock.paused; }
    );
    if (m_clockHover >= 8)
    {
        const int index = clockScroll(scene) + m_clockHover - 8;
        if (index >= static_cast<int>(readings.size()))
            return {};
        if (readings[index].path.size() != 1)
            return {
                "Clock inside a placed subcircuit", "Change its settings in the subcircuit editor."
            };
        return {
            readings[index].paused ? "Enable this clock" : "Disable this clock",
            "Disabled clocks freeze their phase and do not step.",
            "Enabling does not resume globally paused time."
        };
    }
    std::vector<std::string> text;
    switch (static_cast<ClockControl>(m_clockHover))
    {
    case ClockControl::RunPause:
        text = {
            "Run / Pause automatic clock time",
            "Preserves each clock's Enable / Disable setting.",
            "Manual input changes still settle while paused."
        };
        break;
    case ClockControl::SingleStep:
        text = {
            "Half cycle: one clock transition",
            "Advance to the next edge of any enabled clock.",
            "One clock: LOW > HIGH or HIGH > LOW.",
            "Several clocks: one shared edge time.",
            "Clocks due together switch together, then settle.",
            "Leaves automatic time paused."
        };
        break;
    case ClockControl::DoubleStep:
        text = {
            "Full cycle: two clock transitions",
            "Process the next two edge times; settle after each.",
            "One clock: LOW > HIGH > LOW, or the inverse.",
            "Several clocks: two shared edge times.",
            "This may not complete a cycle for every clock.",
            "Leaves automatic time paused."
        };
        break;
    case ClockControl::ZoomOut:
        return {
            "Zoom out in time",
            "Double the displayed time frame.",
            "Only the graph scale changes; clock Hz stays the same.",
            "Unreadable waveforms are hidden. Use Fit or zoom in."
        };
    case ClockControl::ZoomIn:
        return {
            "Zoom in in time",
            "Halve the displayed time frame.",
            "Only the graph scale changes; clock Hz stays the same."
        };
    case ClockControl::Live:
        return {
            "Follow the newest recorded time",
            "Keeps your zoom level. Shift + scroll browses history."
        };
    case ClockControl::Fit:
        return {
            "Fit four cycles of the fastest enabled clock",
            "Time frame = 4 / Hz; follows frequency changes.",
            "Manual zoom stops automatic fitting.",
            "If all clocks are disabled, uses their configured Hz."
        };
    case ClockControl::Clear:
        return {
            "Clear this view's graph recordings",
            "Clock states, phase and simulation time stay unchanged.",
            "New edges continue to be recorded."
        };
    }
    if (!enabled)
        text.push_back(
            readings.empty() ? "No clocks: Run and stepping do nothing."
                             : "All clocks disabled: Run and stepping do nothing."
        );
    else if (static_cast<ClockControl>(m_clockHover) != ClockControl::RunPause)
        text.push_back("Disabled clocks are excluded from stepping.");
    if (scene.getLastEvalResult() != SimulationResult::OK)
        text.push_back("Simulation is blocked. See Messages before stepping.");
    return text;
}

CanvasViewport UI::clockTooltipBounds(const Scene& scene) const
{
    const auto text = clockTooltip(scene);
    if (text.empty())
        return {};
    const double width = std::min(450.0, std::max(0.0, m_surface.windowWidth - 16.0));
    const double height = 16 + static_cast<double>(text.size()) * 20;
    return {
        std::clamp(m_pointer.x, 8.0, std::max(8.0, m_surface.windowWidth - width - 8)),
        std::max(8.0, m_bottom.y - height - 8),
        width,
        height
    };
}
