#pragma once
#include "Components/PinTypes.h"
#include "Geometry/GridCoords.h"
#include "Graphics/Text/TextGeometry.h"
#include "Simulation/NetTypes.h"
#include "Simulation/SimulationStatus.h"

#include <array>
#include <cstddef>

enum class DrawCategory
{
    StaticUI,
    DynamicUI,
    Components,
    Wires
};

/** Actual scene submissions per frame; the debug overlay is excluded. */
struct DrawCallCounts
{
    std::array<int, 4> calls{};

    int total() const { return calls[0] + calls[1] + calls[2] + calls[3]; }

    int operator[](DrawCategory category) const
    {
        return calls[static_cast<std::size_t>(category)];
    }

    bool operator==(const DrawCallCounts&) const = default;
};

enum class CpuPhase
{
    Input,
    Simulation,
    VisualPreparation,
    StaticUI,
    DynamicUI,
    Canvas,
    FrameWait,
    DebugOverlay,
    Propagation,
    Count
};

struct CpuTimings
{
    std::array<double, static_cast<std::size_t>(CpuPhase::Count)> ms{};

    double& operator[](CpuPhase phase) { return ms[static_cast<std::size_t>(phase)]; }

    double operator[](CpuPhase phase) const { return ms[static_cast<std::size_t>(phase)]; }
};

/** Fixed one-second sampling windows; no sample history or per-component timers. */
class CpuTimingWindow
{
  public:
    void add(const CpuTimings& timings, double elapsedSeconds);
    void reset();

    const CpuTimings& average() const { return m_average; }

    const CpuTimings& worst() const { return m_worst; }

    bool ready() const { return m_ready; }

  private:
    CpuTimings m_sum, m_peak, m_average, m_worst;
    double m_elapsed = 0;
    std::size_t m_samples = 0;
    bool m_ready = false;
};

// Debugging metrics passed to HUD
struct DebugMetrics
{
    float fps = 0;
    float frameTimeMs = 0;
    DrawCallCounts drawCalls;
    CpuTimings averageCpu, worstCpu;
    bool timingsReady = false;
    SimulationWork simulationWork;
    double pendingSimulationMs = 0;
    size_t totalComponents = 0;
    SimulationResult evalResult = SimulationResult::OK;
    size_t netCount = 0;
    size_t wireCount = 0;
    size_t shortedNetCount = 0;
    size_t rejectedConnectionCount = 0;
    int hoveredCompId = -1;
    int hoveredPinComponentId = -1;
    int hoveredPinIdx = -1;
    PinType hoveredPinType = PinType::INPUT;
    WireId hoveredWireId = INVALID_WIRE_ID;
    int selectedCompId = -1;
    GridCoords cursorGrid{0, 0};
};

/** @brief Formats diagnostics and lays out right-aligned screen text; warnings survive F3. */
std::vector<TextRun> layoutDebugOverlay(
    const DebugMetrics& metrics, bool showMetrics, int width, int height, const FontMetrics& font
);
