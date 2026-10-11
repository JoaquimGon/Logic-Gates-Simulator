#include "Editor/Scene.h"
#include "Graphics/Renderer.h"
#include "UI/UI.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <sstream>

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
    const double start = clockStartTime(), end = start + window;
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
    if (!readings.empty() && clear.x - statusX > 140)
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
    for (const auto& clock : readings)
        dense |= clockWaveformTooDense(clock.frequency);
    if (wide)
        label(
            m_clockError.empty() ? (dense            ? "Hidden: Fit or zoom in"
                                    : m_clockAutoFit ? "Auto Fit | Shift: history"
                                                     : "Shift + scroll: history")
                                 : m_clockError,
            {list.x, m_bottom.y + 67, clockLabelWidth() - 8, 18},
            m_clockError.empty() ? muted : glm::vec4{1, 0.55f, 0.4f, 1},
            0.28f
        );
    renderer.drawText(text, TextSpace::Screen);
    text.clear();
    renderer.setScreenClip(list);
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
    auto xAt = [&](double time) { return graphX + (time - start) / window * graphWidth; };
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
        auto unreadable = [&]()
        {
            // Do not generate any waveform/guide/cursor geometry for this row.
            const std::string title =
                graphWidth >= 340 ? "Too fast for this time frame." : "Too fast to display";
            const std::string hint = "Zoom in or Fit";
            for (int line = 0; line < 2; ++line)
            {
                const auto& value = line == 0 ? title : hint;
                const float scale = std::min(
                    0.32f,
                    static_cast<float>(std::max(0.0, graphWidth - 12)) /
                        std::max(1.0f, getTextWidth(value, 1, renderer.fontMetrics()))
                );
                label(
                    value,
                    {graphX, y + 3 + line * 20, graphWidth, 20},
                    {0.9f, 0.72f, 0.38f, 1},
                    scale
                );
            }
        };
        if (clockWaveformTooDense(clock.frequency))
        {
            unreadable();
            continue;
        }
        auto found = history.traces().find(clock.path);
        if (found == history.traces().end() || graphWidth <= 0)
            continue;
        const auto& trace = found->second;
        if (trace.empty())
            continue;
        const double stop = std::min(end, history.time());
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
            unreadable();
            continue;
        }
        const auto baseline = first == trace.begin() ? trace.begin() : std::prev(first);
        double from = std::max(start, baseline->time);
        bool high = baseline->high;
        auto level = [&](bool state) { return y + (state ? 9 : 35); };
        auto interval = [&](double to)
        {
            if (to > from)
                lines.push_back({{xAt(from), level(high), xAt(to) - xAt(from), 2}, green});
        };
        for (auto it = first; it != last; ++it)
        {
            interval(it->time);
            const double edgeX = xAt(it->time);
            lines.push_back({{edgeX, y + 5, 1, 36}, {0.24f, 0.31f, 0.4f, 0.7f}});
            lines.push_back({{edgeX, y + 9, 1.5, 28}, green});
            from = it->time;
            high = it->high;
        }
        interval(stop);
        if (history.time() >= start && history.time() <= end)
            lines.push_back({{xAt(history.time()) - 1, y + 4, 2, 39}, {0.6f, 0.8f, 1, 0.85f}});
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
