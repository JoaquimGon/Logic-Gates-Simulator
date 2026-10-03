#pragma once
#include "Components/PinTypes.h"
#include "Geometry/GridCoords.h"
#include "Graphics/Text/TextGeometry.h"
#include "Simulation/NetTypes.h"
#include "Simulation/SimulationStatus.h"

#include <cstddef>

// Debugging metrics passed to HUD
struct DebugMetrics
{
    float fps = 0;
    float frameTimeMs = 0;
    int drawCalls = 0;
    float lastPropagateMs = 0;
    float timeSinceLastPropagateMs = 0;
    size_t scheduledComponentCount = 0;
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
