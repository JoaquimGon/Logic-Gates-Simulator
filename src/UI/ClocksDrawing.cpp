#include "Editor/Scene.h"
#include "Graphics/Renderer.h"
#include "UI/UI.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace
{
// Shared bounded geometry for clock and selected-pin waveforms.
bool appendWaveform(
    const std::deque<ClockSample>& trace,
    double start,
    double window,
    double now,
    double graphX,
    double graphWidth,
    double y,
    std::vector<Renderer::ScreenRect>& lines,
    bool terminal = false
)
{
    if (trace.empty() || graphWidth <= 0)
        return true;
    const glm::vec4 green{0.35f, 0.88f, 0.5f, 1};
    auto xAt = [&](double time) { return graphX + (time - start) / window * graphWidth; };
    const double stop = std::min(start + window, now);
    const auto first = std::upper_bound(
        trace.begin(),
        trace.end(),
        start,
        [](double time, const ClockSample& sample) { return time < sample.time; }
    );
    const auto last = std::upper_bound(
        first,
        trace.end(),
        stop,
        [](double time, const ClockSample& sample) { return time < sample.time; }
    );
    const int edgeCount = static_cast<int>(std::distance(first, last));
    bool tooDense = edgeCount > std::min(128.0, graphWidth / 8.0);
    // Previous frequency settings may have left a dense region in the saved session history.
    // Scan at most 128 visible edges; a suppressed row never builds geometry.
    if (!tooDense && first != last)
        for (auto it = std::next(first); it != last; ++it)
            if ((it->time - std::prev(it)->time) / window * graphWidth < 8)
            {
                tooDense = true;
                break;
            }
    if (tooDense)
    {
        return false;
    }
    const auto baseline = first == trace.begin() ? trace.begin() : std::prev(first);
    double from = std::max(start, baseline->time);
    bool high = baseline->high;
    bool available = baseline->available;
    auto level = [&](bool state) { return y + (state ? 9 : 35); };
    auto interval = [&](double to)
    {
        if (to > from)
            lines.push_back(
                {{xAt(from), available ? level(high) : y + 22, xAt(to) - xAt(from), 2},
                 available ? green : glm::vec4{0.55f, 0.65f, 0.76f, 0.65f}}
            );
    };
    for (auto it = first; it != last; ++it)
    {
        interval(it->time);
        const double edgeX = xAt(it->time);
        lines.push_back({{edgeX, y + 5, 1, 36}, {0.24f, 0.31f, 0.4f, 0.7f}});
        if (available && it->available && high != it->high)
            lines.push_back({{edgeX, y + 9, 1.5, 28}, green});
        from = it->time;
        high = it->high;
        available = it->available;
    }
    interval(stop);
    if (now >= start && now <= start + window)
    {
        lines.push_back({{xAt(now) - 1, y + 4, 2, 39}, {0.6f, 0.8f, 1, 0.85f}});
        if (terminal)
            lines.push_back(
                {{xAt(now), available ? level(high) : y + 22, 6, 2},
                 available ? green : glm::vec4{0.55f, 0.65f, 0.76f, 0.65f}}
            );
    }
    return true;
}

void appendClockPreview(
    const std::deque<ClockSample>& preview,
    double start,
    double window,
    double graphX,
    double graphWidth,
    double y,
    std::vector<Renderer::ScreenRect>& lines
)
{
    if (preview.empty() || graphWidth <= 0)
        return;
    const glm::vec4 forecast{0.35f, 0.88f, 0.5f, 0.4f};
    const double dash = std::max(8.0, graphWidth / 256);
    auto xAt = [&](double time) { return graphX + (time - start) / window * graphWidth; };
    double from = preview.front().time;
    bool high = preview.front().high;
    auto interval = [&](double to)
    {
        const double end = xAt(to);
        for (double x = xAt(from); x < end; x += dash)
            lines.push_back({{x, y + (high ? 9 : 35), std::min(dash / 2, end - x), 2}, forecast});
    };
    for (auto it = std::next(preview.begin()); it != preview.end(); ++it)
    {
        interval(it->time);
        const double x = xAt(it->time);
        for (double edgeY = y + 9; edgeY < y + 37; edgeY += 8)
            lines.push_back({{x, edgeY, 1.5, 4}, forecast});
        from = it->time;
        high = it->high;
    }
    interval(start + window);
}
} // namespace

void UI::drawClocks(Renderer& renderer, const Scene& scene) const
{
    renderer.setUiDrawCategory(DrawCategory::DynamicUI);
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
        scene.clocksPaused() ? "Run" : "Pause",
        "Half cycle",
        "Full cycle",
        "-",
        "+",
        "Live",
        "Fit",
        "Clear"
    };
    for (auto control :
         {ClockControl::RunPause,
          ClockControl::SingleStep,
          ClockControl::DoubleStep,
          ClockControl::ZoomOut,
          ClockControl::ZoomIn,
          ClockControl::Live,
          ClockControl::Fit,
          ClockControl::Clear})
    {
        const auto bounds = clockControlBounds(control);
        const bool hovered = bounds.contains(m_pointer.x, m_pointer.y);
        renderer.drawScreenRect(
            bounds, hovered ? glm::vec4{0.2f, 0.4f, 0.6f, 1} : glm::vec4{0.16f, 0.3f, 0.46f, 1}
        );
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
    const double window = clockWindowSeconds();
    const double start = clockStartTime();
    const auto readings = scene.clockReadings();
    auto timeText = [](double value)
    {
        std::ostringstream out;
        out << std::setprecision(5) << value << " s";
        return out.str();
    };
    const auto fit = clockControlBounds(ClockControl::Fit);
    const auto clear = clockControlBounds(ClockControl::Clear);
    const double statusX = fit.x + fit.width + 8;
    if ((!readings.empty() || !clockTimeline().signals().empty()) && clear.x - statusX > 140)
        label(
            "Elapsed: " + timeText(historyTime),
            {statusX, m_bottom.y + 40, clear.x - statusX - 8, 24},
            muted
        );
    const bool wide = graphWidth > 300;
    const auto frameBox =
        CanvasViewport{wide ? graphX : list.x, m_bottom.y + 67, wide ? graphWidth : list.width, 18};
    const auto frameText = clockTimeFrameText();
    const float frameScale = std::min(
        0.32f,
        static_cast<float>(std::max(0.0, frameBox.width - 12)) /
            std::max(1.0f, getTextWidth(frameText, 1, renderer.fontMetrics()))
    );
    const double frameWidth = getTextWidth(frameText, frameScale, renderer.fontMetrics()) + 8;
    label(
        frameText,
        {frameBox.x + (frameBox.width - frameWidth) / 2, frameBox.y, frameWidth, 18},
        ink,
        frameScale
    );
    bool dense = false;
    if (m_bottomTab == BottomTab::Clocks)
        for (const auto& clock : readings)
            dense |= clockWaveformTooDense(clock.frequency);
    if (wide)
        label(
            m_clockError.empty()
                ? (dense ? "Preview: Fit or zoom in"
                   : m_bottomTab == BottomTab::Clocks
                       ? (scene.clocksPaused() ? "Preview on Run | Shift: history"
                                               : "Dashed: preview | Shift: history")
                       : "Shift + scroll: history")
                : m_clockError,
            {list.x, m_bottom.y + 67, clockLabelWidth() - 8, 18},
            m_clockError.empty() ? muted : glm::vec4{1, 0.55f, 0.4f, 1},
            0.28f
        );
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(list);
    if (m_bottomTab == BottomTab::Signals)
    {
        drawSignalRows(renderer);
        renderer.setScreenClip(m_bottom);
        renderer.setUiDrawCategory(DrawCategory::StaticUI);
        return;
    }
    if (readings.empty())
    {
        label("No clocks in this circuit.", list, muted);
        renderer.drawText(text, TextSpace::Screen);
        renderer.setScreenClip(m_bottom);
        renderer.setUiDrawCategory(DrawCategory::StaticUI);
        return;
    }
    const int offset = clockScroll(scene);
    const auto& history = clockTimeline();
    for (int row = 0; row + offset < static_cast<int>(readings.size()) && row * 48 < list.height;
         ++row)
    {
        const auto& clock = readings[row + offset];
        const double y = list.y + row * 48;
        const bool native = !m_readOnly && clock.path.size() == 1;
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
                    (clock.paused ? "disabled" : "enabled") +
                    (m_readOnly ? " | read only" : " | edit inside"),
                {list.x, y + 23, clockLabelWidth() - 8, 22},
                muted,
                0.28f
            );
        lines.push_back({{list.x, y + 47, list.width, 1}, {0.18f, 0.23f, 0.3f, 1}});
        const double cursorX =
            graphX + std::clamp((history.time() - start) / window, 0.0, 1.0) * graphWidth;
        auto unreadable = [&](bool future)
        {
            const double x = future ? cursorX : graphX;
            const double width = future ? graphX + graphWidth - cursorX : cursorX - graphX;
            if (width <= 0)
                return;
            const std::string title = future ? "Preview too fast" : "History too dense";
            const std::string hint = "Zoom in or Fit";
            for (int line = 0; line < 2; ++line)
            {
                const auto& value = line == 0 ? title : hint;
                const float scale = std::min(
                    0.32f,
                    static_cast<float>(std::max(0.0, width - 12)) /
                        std::max(1.0f, getTextWidth(value, 1, renderer.fontMetrics()))
                );
                label(value, {x, y + 3 + line * 20, width, 20}, {0.9f, 0.72f, 0.38f, 1}, scale);
            }
        };
        auto found = history.traces().find(clock.path);
        if (found == history.traces().end() || graphWidth <= 0)
            continue;
        const auto& trace = found->second;
        if (trace.empty())
            continue;
        if (!appendWaveform(trace, start, window, history.time(), graphX, graphWidth, y, lines))
        {
            unreadable(false);
            if (history.time() >= start && history.time() <= start + window)
                lines.push_back({{cursorX - 1, y + 4, 2, 39}, {0.6f, 0.8f, 1, 0.85f}});
        }
        if (scene.getLastEvalResult() == SimulationResult::OK && !scene.isSimulationDirty())
        {
            if (clockWaveformTooDense(clock.frequency))
                unreadable(true);
            else
                appendClockPreview(
                    clockPreview(clock), start, window, graphX, graphWidth, y, lines
                );
        }
    }
    renderer.drawScreenRects(lines);
    renderer.drawText(text, TextSpace::Screen);
    renderer.setScreenClip(m_bottom);
    renderer.setUiDrawCategory(DrawCategory::StaticUI);
}

void UI::drawClockTooltip(Renderer& renderer, const Scene& scene) const
{
    const auto info = clockTooltip(scene);
    if (info.empty())
        return;
    renderer.setUiDrawCategory(DrawCategory::DynamicUI);
    const auto bounds = clockTooltipBounds(scene);
    renderer.setScreenClip(bounds);
    renderer.drawScreenRect(bounds, {0.3f, 0.45f, 0.6f, 1});
    renderer.drawScreenRect(
        {bounds.x + 1, bounds.y + 1, bounds.width - 2, bounds.height - 2}, {0.08f, 0.12f, 0.17f, 1}
    );
    std::vector<TextRun> text;
    for (std::size_t row = 0; row < info.size(); ++row)
    {
        const float scale = std::min(
            0.34f,
            static_cast<float>(std::max(0.0, bounds.width - 18)) /
                std::max(1.0f, getTextWidth(info[row], 1, renderer.fontMetrics()))
        );
        text.push_back(
            {info[row],
             {static_cast<float>(bounds.x + 8),
              static_cast<float>(bounds.y + 8 + row * 20 + 10) +
                  getCapHeight(scale, renderer.fontMetrics()) / 2},
             scale,
             row == 0 ? glm::vec4{0.87f, 0.91f, 0.97f, 1} : glm::vec4{0.65f, 0.75f, 0.85f, 1}}
        );
    }
    renderer.drawText(text, TextSpace::Screen);
    renderer.setScreenClip(std::nullopt);
    renderer.setUiDrawCategory(DrawCategory::StaticUI);
}

void UI::drawSignalRows(Renderer& renderer) const
{
    const auto list = clockListBounds();
    const auto& signals = clockTimeline().signals();
    const glm::vec4 ink{0.87f, 0.91f, 0.97f, 1}, muted{0.55f, 0.65f, 0.76f, 1};
    std::vector<TextRun> text;
    std::vector<Renderer::ScreenRect> lines;
    auto label = [&](std::string value, CanvasViewport box, glm::vec4 color)
    {
        const auto& font = renderer.fontMetrics();
        while (!value.empty() && getTextWidth(value, 0.3f, font) > box.width - 8)
        {
            value.resize(value.size() > 3 ? value.size() - 4 : 0);
            if (!value.empty())
                value += "...";
        }
        text.push_back(
            {value, {static_cast<float>(box.x + 4), static_cast<float>(box.y + 14)}, 0.3f, color}
        );
    };
    if (signals.empty())
        label("Right-click a component and Trace a pin (up to 8).", list, muted);
    const int offset = std::clamp(
        m_clockScroll,
        0,
        std::max(
            0, static_cast<int>(signals.size()) - std::max(1, static_cast<int>(list.height / 48))
        )
    );
    const double graphX = list.x + clockLabelWidth();
    const double graphWidth = std::max(0.0, list.width - clockLabelWidth());
    for (int row = 0; row + offset < static_cast<int>(signals.size()) && row * 48 < list.height;
         ++row)
    {
        const auto& signal = signals[row + offset];
        const double y = list.y + row * 48;
        label(signal.label, {list.x, y, clockLabelWidth() - 8, 20}, ink);
        std::string status = "Unavailable";
        if (!signal.samples.empty() && signal.samples.back().available)
            status = signal.samples.back().high ? "HIGH (1)" : "LOW (0)";
        if (signal.samples.size() > 1 &&
            signal.samples.back().time == signal.samples[signal.samples.size() - 2].time)
            status += " | paused changes";
        label(status, {list.x, y + 23, clockLabelWidth() - 32, 20}, muted);
        const auto remove = signalRemoveBounds(row);
        renderer.drawScreenRect(remove, {0.12f, 0.2f, 0.29f, 1});
        label("X", remove, ink);
        if (!appendWaveform(
                signal.samples,
                clockStartTime(),
                clockWindowSeconds(),
                clockTimeline().time(),
                graphX,
                graphWidth,
                y,
                lines,
                true
            ))
            label(
                "Too dense: zoom in or Fit",
                {graphX, y + 12, graphWidth, 20},
                {0.9f, 0.72f, 0.38f, 1}
            );
        lines.push_back({{list.x, y + 47, list.width, 1}, {0.18f, 0.23f, 0.3f, 1}});
    }
    renderer.drawScreenRects(lines);
    renderer.drawText(text, TextSpace::Screen);
}
