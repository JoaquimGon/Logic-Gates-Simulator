#include "Editor/Scene.h"
#include "Graphics/Renderer.h"
#include "UI/UI.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

void UI::drawClocks(Renderer& renderer, const Scene& scene) const
{
    std::vector<TextRun> text;
    std::vector<Renderer::ScreenRect> lines;
    const glm::vec4 ink{0.87f, 0.91f, 0.97f, 1};
    const glm::vec4 muted{0.55f, 0.65f, 0.76f, 1};
    const glm::vec4 green{0.35f, 0.88f, 0.5f, 1};
    auto label = [&](std::string value, CanvasViewport box, glm::vec4 color, float scale = 0.34f)
    {
        const auto& font = renderer.fontMetrics();
        // Shorten long nested paths instead of making the text illegibly small.
        if (getTextWidth(value, scale, font) > box.width - 8)
        {
            while (value.size() > 0 && getTextWidth(value + "...", scale, font) > box.width - 8)
                value.pop_back();
            value += "...";
        }
        text.push_back(
            {value,
             {static_cast<float>(box.x + 4),
              static_cast<float>(box.y + box.height / 2) + getCapHeight(scale, font) / 2},
             scale,
             color}
        );
    };
    const char* names[] = {
        scene.clocksPaused() ? "Run" : "Pause", "1 edge", "2 edges", "-", "+", "Live"
    };
    for (auto control :
         {ClockControl::RunPause,
          ClockControl::SingleStep,
          ClockControl::DoubleStep,
          ClockControl::ZoomOut,
          ClockControl::ZoomIn,
          ClockControl::Live})
    {
        const auto bounds = clockControlBounds(control);
        renderer.drawScreenRect(bounds, {0.16f, 0.3f, 0.46f, 1});
        const auto name = names[static_cast<int>(control)];
        const float scale = std::min(
            0.34f,
            static_cast<float>(std::max(0.0, bounds.width - 10)) /
                std::max(1.0f, getTextWidth(name, 1, renderer.fontMetrics()))
        );
        label(name, bounds, ink, scale);
    }
    const auto list = clockListBounds();
    const double graphX = list.x + clockLabelWidth();
    const double graphWidth = std::max(0.0, list.x + list.width - graphX);
    const double historyTime = clockTimeline().time();
    const double start = clockStartTime(), end = start + m_clockWindow;
    auto timeText = [](double value)
    {
        std::ostringstream out;
        out << std::setprecision(5) << value << " s";
        return out.str();
    };
    const auto live = clockControlBounds(ClockControl::Live);
    const double statusX = live.x + live.width + 8;
    if (m_bottom.x + m_bottom.width - statusX > 100)
        label(
            "t = " + timeText(historyTime) + (scene.clocksPaused() ? " | paused" : " | running"),
            {statusX, m_bottom.y + 40, m_bottom.x + m_bottom.width - statusX - 8, 24},
            muted
        );
    label(
        m_clockError.empty() ? "Scroll; Shift: time" : m_clockError,
        {list.x, m_bottom.y + 67, clockLabelWidth() - 8, 18},
        m_clockError.empty() ? muted : glm::vec4{1, 0.55f, 0.4f, 1},
        0.28f
    );
    label(timeText(start), {graphX, m_bottom.y + 67, graphWidth / 2, 18}, muted, 0.28f);
    const auto endText = timeText(end);
    const double endWidth = std::min(
        graphWidth / 2,
        static_cast<double>(getTextWidth(endText, 0.28f, renderer.fontMetrics())) + 8
    );
    label(endText, {graphX + graphWidth - endWidth, m_bottom.y + 67, endWidth, 18}, muted, 0.28f);
    if (graphWidth > 250)
        label(
            timeText((start + end) / 2),
            {graphX + graphWidth / 2 - 24, m_bottom.y + 67, 90, 18},
            muted,
            0.28f
        );
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(list);
    const auto readings = scene.clockReadings();
    if (readings.empty())
        label("No clocks in this circuit.", list, muted);
    const int offset = clockScroll(scene);
    const auto& history = clockTimeline();
    auto xAt = [&](double time) { return graphX + (time - start) / m_clockWindow * graphWidth; };
    // Densely packed steps share a pixel guide; the recorded transitions remain exact.
    int lastGuide = -1;
    for (double time : history.steps())
        if (time >= start && time <= end)
        {
            const int column = static_cast<int>(xAt(time));
            if (column != lastGuide)
                lines.push_back(
                    {{static_cast<double>(column), list.y, 1, list.height},
                     {0.24f, 0.31f, 0.4f, 0.7f}}
                );
            lastGuide = column;
        }
    for (int row = 0; row + offset < static_cast<int>(readings.size()) && row * 48 < list.height;
         ++row)
    {
        const auto& clock = readings[row + offset];
        const double y = list.y + row * 48;
        const bool native = clock.path.size() == 1;
        label(scene.clockName(clock.path), {list.x, y, clockLabelWidth() - 76, 20}, ink);
        label(
            clock.high ? "HIGH (1)" : "LOW (0)",
            {list.x + clockLabelWidth() - 76, y, 72, 20},
            clock.high ? green : muted,
            0.3f
        );
        const auto frequency = clockFrequencyBounds(row), pause = clockPauseBounds(row);
        if (native)
        {
            const bool editing = m_clockEditing == clock.path.front();
            renderer.drawScreenRect(
                frequency,
                editing ? glm::vec4{0.16f, 0.3f, 0.46f, 1} : glm::vec4{0.11f, 0.15f, 0.21f, 1}
            );
            label(
                (editing ? m_clockDraft + "_" : frequencyText(clock.frequency)) + " Hz",
                frequency,
                ink
            );
            renderer.drawScreenRect(pause, {0.11f, 0.15f, 0.21f, 1});
            label(clock.paused ? "Enable" : "Disable", pause, clock.paused ? muted : ink);
        }
        else
            label(
                frequencyText(clock.frequency) + " Hz | " +
                    (clock.paused ? "disabled" : "enabled") + " | edit inside",
                {list.x, y + 23, clockLabelWidth() - 8, 22},
                muted,
                0.28f
            );
        lines.push_back({{list.x, y + 47, list.width, 1}, {0.18f, 0.23f, 0.3f, 1}});
        auto found = history.traces().find(clock.path);
        if (found == history.traces().end() || graphWidth <= 0)
            continue;
        const auto& trace = found->second;
        if (trace.empty())
            continue;
        auto level = [&](bool high) { return y + (high ? 9 : 35); };
        double from = std::max(start, trace.front().time);
        bool state = trace.front().high;
        int lastEdgeColumn = -1;
        for (std::size_t i = 1; i < trace.size(); ++i)
        {
            const auto& sample = trace[i];
            if (sample.time < start)
            {
                state = sample.high;
                continue;
            }
            if (sample.time > end || sample.time > history.time())
                break;
            const double x0 = std::max(graphX, xAt(from)), x1 = xAt(sample.time);
            if (x1 > x0)
                lines.push_back({{x0, level(state), x1 - x0, 2}, green});
            const int column = static_cast<int>(x1);
            if (column != lastEdgeColumn)
                lines.push_back({{x1, y + 9, 2, 28}, green});
            lastEdgeColumn = column;
            state = sample.high;
            from = sample.time;
        }
        const double stop = std::min(end, history.time());
        if (stop > from)
            lines.push_back(
                {{std::max(graphX, xAt(from)),
                  level(state),
                  xAt(stop) - std::max(graphX, xAt(from)),
                  2},
                 green}
            );
    }
    renderer.drawScreenRects(lines);
    renderer.drawText(text, TextSpace::Screen);
    if (history.time() >= start && history.time() <= end)
        renderer.drawScreenRect(
            {xAt(history.time()) - 1, list.y, 2, list.height}, {0.6f, 0.8f, 1, 0.85f}
        );
    renderer.setScreenClip(m_bottom);
}
