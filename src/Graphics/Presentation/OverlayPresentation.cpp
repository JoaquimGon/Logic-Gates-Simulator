#include "Graphics/Presentation/OverlayPresentation.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>


std::vector<TextRun>
layoutDebugOverlay(const DebugMetrics& metrics, bool showMetrics, int width, int height, const FontMetrics& font)
{
    if (width <= 0 || height <= 0)
        return {};
    // Frame pacing and the main CPU timing table.
    std::ostringstream ssL1;
    ssL1 << std::fixed << std::setprecision(1) << "FPS: " << metrics.fps << " (" << metrics.frameTimeMs
         << " ms) | Draw Calls: " << metrics.drawCalls.total();

    std::ostringstream simulation;
    simulation << "Simulation: " << metrics.totalComponents << " components";
    simulation << (metrics.evalResult == SimulationResult::OK ? " [OK]" : " [BLOCKED]");

    std::ostringstream ssL4;
    ssL4 << "Topology: " << metrics.netCount << " Nets | " << metrics.wireCount << " Wires | "
         << metrics.shortedNetCount << " Shorts | " << metrics.rejectedConnectionCount << " Rejected";

    std::ostringstream ssL5;
    if (metrics.selectedCompId != -1)
        ssL5 << "Selected: Comp #" << metrics.selectedCompId;
    else if (metrics.hoveredPinComponentId != -1)
        ssL5 << "Hover: Comp #" << metrics.hoveredPinComponentId << " Pin "
             << (metrics.hoveredPinType == PinType::INPUT ? "In[" : "Out[") << metrics.hoveredPinIdx << "]";
    else if (metrics.hoveredCompId != -1)
        ssL5 << "Hover: Comp #" << metrics.hoveredCompId;
    else if (metrics.hoveredWireId != INVALID_WIRE_ID)
        ssL5 << "Hover: Wire #" << metrics.hoveredWireId;
    else
        ssL5 << "Hover: None";

    ssL5 << " | Grid: (" << metrics.cursorGrid.x << ", " << metrics.cursorGrid.y << ")";

    const glm::vec4 ink{0.85f, 0.85f, 0.85f, 0.95f};
    const glm::vec4 muted{0.65f, 0.72f, 0.8f, 0.95f};
    const glm::vec4 accent{1, 0.7f, 0.3f, 1};
    std::vector<std::pair<std::string, glm::vec4>> lines = {
        {"[PERFORMANCE] (F3)", accent},
        {ssL1.str(), ink},
        {metrics.timingsReady ? "CPU / frame (ms)          Average     Worst"
                              : "CPU / frame (ms) - sampling first second...",
         muted}
    };
    const char* names[] = {"Input / UI layout", "Simulation",         "Visual preparation", "Static UI",   "Dynamic UI",
                           "Canvas submission", "Frame wait / VSync", "Debug overlay",      "Propagation*"};
    for (std::size_t i = 0; i < metrics.averageCpu.ms.size(); ++i)
    {
        std::ostringstream row;
        row << std::left << std::setw(24) << names[i] << std::right << std::fixed << std::setprecision(3)
            << std::setw(9) << metrics.averageCpu.ms[i] << std::setw(10) << metrics.worstCpu.ms[i];
        lines.push_back({row.str(), metrics.worstCpu.ms[i] >= 4 ? accent : ink});
    }
    lines.push_back({"*Included in simulation/input; CPU, not GPU time", muted});
    std::ostringstream work;
    work << "Work/frame: " << metrics.simulationWork.propagationCalls << " calls | "
         << metrics.simulationWork.evaluations << " evaluations";
    lines.push_back({work.str(), ink});
    std::ostringstream clocks;
    clocks << "Outer rounds: " << metrics.simulationWork.rounds << " | Edge times: " << metrics.simulationWork.edgeTimes
           << " | Slices: " << metrics.simulationWork.clockSlices;
    lines.push_back({clocks.str(), ink});
    std::ostringstream backlog;
    backlog << "Backlog: " << std::fixed << std::setprecision(2) << metrics.pendingSimulationMs << " ms | Limit: "
            << (metrics.simulationWork.clockLimit == ClockWorkLimit::TimeBudget   ? "4 ms target"
                : metrics.simulationWork.clockLimit == ClockWorkLimit::SliceLimit ? "256 slices"
                                                                                  : "none");
    lines.push_back({backlog.str(), metrics.pendingSimulationMs > 0 ? accent : muted});
    std::ostringstream draws;
    draws << "Draws/frame: Static " << metrics.drawCalls[DrawCategory::StaticUI] << " | Dynamic "
          << metrics.drawCalls[DrawCategory::DynamicUI] << " | Components "
          << metrics.drawCalls[DrawCategory::Components] << " | Wires " << metrics.drawCalls[DrawCategory::Wires];
    lines.push_back({draws.str(), muted});
    lines.push_back({"Draw counts exclude HUD; they do not measure cost", muted});
    lines.push_back(
        {simulation.str(),
         metrics.evalResult == SimulationResult::OK ? glm::vec4{0.35f, 0.9f, 0.45f, 1} : glm::vec4{1, 0.25f, 0.25f, 1}}
    );
    lines.push_back({ssL4.str(), muted});
    lines.push_back({ssL5.str(), muted});

    if (!showMetrics)
        lines.clear();
    if (metrics.evalResult != SimulationResult::OK)
    {
        const glm::vec4 errorColor(1.0f, 0.25f, 0.25f, 1.0f);
        const std::string error = metrics.evalResult == SimulationResult::NON_CONVERGENT
                                      ? "[SIMULATION PAUSED] Signals did not settle"
                                      : "[SIMULATION PAUSED] Connection rejected";
        lines.insert(lines.begin(), {error, errorColor});
        lines.insert(lines.begin() + 1, {"Change an input or fix the circuit to resume.", errorColor});
    }


    std::vector<TextRun> runs;
    constexpr float maximumScale = 0.32f;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const float scale = std::min(
            maximumScale, std::max(0.0f, width - 32.0f) / std::max(1.0f, getTextWidth(lines[i].first, 1, font))
        );
        runs.push_back(
            {lines[i].first,
             {static_cast<float>(width) - 16 - getTextWidth(lines[i].first, scale, font),
              24 + static_cast<float>(i) * 18},
             scale,
             lines[i].second}
        );
    }
    return runs;
}


void CpuTimingWindow::add(const CpuTimings& timings, double elapsedSeconds)
{
    for (std::size_t i = 0; i < timings.ms.size(); ++i)
    {
        m_sum.ms[i] += timings.ms[i];
        m_peak.ms[i] = std::max(m_peak.ms[i], timings.ms[i]);
    }
    ++m_samples;
    m_elapsed += std::max(0.0, elapsedSeconds);
    if (m_elapsed < 1)
        return;
    for (std::size_t i = 0; i < timings.ms.size(); ++i)
        m_average.ms[i] = m_sum.ms[i] / m_samples;
    m_worst = m_peak;
    m_ready = true;
    m_sum = m_peak = {};
    m_samples = 0;
    m_elapsed = 0;
}


void CpuTimingWindow::reset()
{
    *this = {};
}
