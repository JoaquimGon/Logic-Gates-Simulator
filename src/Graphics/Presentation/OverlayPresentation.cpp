#include "Graphics/Presentation/OverlayPresentation.h"

#include <iomanip>
#include <sstream>
#include <utility>

std::vector<TextRun> layoutDebugOverlay(
    const DebugMetrics& metrics, bool showMetrics, int width, int height, const FontMetrics& font
)
{
    if (width <= 0 || height <= 0)
        return {};
    // Line 1: FPS, Frame Time, and Draw Calls
    std::ostringstream ssL1;
    ssL1 << std::fixed << std::setprecision(1) << "FPS: " << metrics.fps << " ("
         << metrics.frameTimeMs << " ms) | Draw Calls: " << metrics.drawCalls;

    // Line 2: Propagation Duration & Latency
    std::ostringstream ssL2;
    ssL2 << std::fixed << std::setprecision(3) << "Propagate Exec: " << metrics.lastPropagateMs
         << " ms (Last Call: ";
    if (metrics.timeSinceLastPropagateMs >= 999.0f)
        ssL2 << ">999 ms ago)";
    else
        ssL2 << std::fixed << std::setprecision(0) << metrics.timeSinceLastPropagateMs
             << " ms ago)";

    // Line 3: Evaluation Order & Topological Status
    std::ostringstream ssL3;
    ssL3 << "Eval Order: " << metrics.evalOrderCount << "/" << metrics.totalComponents
         << " components";
    if (metrics.evalResult == EvalOrderResult::CYCLE_DETECTED)
        ssL3 << " [CYCLE DETECTED]";
    else if (metrics.evalResult == EvalOrderResult::CONNECTION_REJECTED)
        ssL3 << " [CONNECTION REJECTED]";
    else
        ssL3 << " [OK]";

    // Line 4: Electrical Topology & Short Contention
    std::ostringstream ssL4;
    ssL4 << "Topology: " << metrics.netCount << " Nets | " << metrics.wireCount << " Wires | "
         << metrics.shortedNetCount << " Shorts | " << metrics.rejectedConnectionCount
         << " Rejected";

    // Line 5: Interaction / Selection & Cursor Position
    std::ostringstream ssL5;
    if (metrics.selectedCompId != -1)
        ssL5 << "Selected: Comp #" << metrics.selectedCompId;
    else if (metrics.hoveredPinComponentId != -1)
        ssL5 << "Hover: Comp #" << metrics.hoveredPinComponentId << " Pin "
             << (metrics.hoveredPinType == PinType::INPUT ? "In[" : "Out[") << metrics.hoveredPinIdx
             << "]";
    else if (metrics.hoveredCompId != -1)
        ssL5 << "Hover: Comp #" << metrics.hoveredCompId;
    else if (metrics.hoveredWireId != INVALID_WIRE_ID)
        ssL5 << "Hover: Wire #" << metrics.hoveredWireId;
    else
        ssL5 << "Hover: None";

    ssL5 << " | Grid: (" << metrics.cursorGrid.x << ", " << metrics.cursorGrid.y << ")";

    std::vector<std::pair<std::string, glm::vec4>> lines = {
        {"[DEBUG HUD] (F3)", glm::vec4(1.0f, 0.62f, 0.11f, 1.0f)}, // Orange Header
        {ssL1.str(), glm::vec4(0.85f, 0.85f, 0.85f, 0.95f)},
        {ssL2.str(),
         (metrics.timeSinceLastPropagateMs < 50.0f) ? glm::vec4(0.35f, 0.90f, 0.45f, 0.95f)
                                                    : glm::vec4(0.70f, 0.70f, 0.75f, 0.85f)},
        {ssL3.str(),
         (metrics.evalResult != EvalOrderResult::OK) ? glm::vec4(1.0f, 0.25f, 0.25f, 1.0f)
                                                     : glm::vec4(0.35f, 0.90f, 0.45f, 0.95f)},
        {ssL4.str(),
         (metrics.shortedNetCount > 0)
             ? glm::vec4(1.0f, 0.25f, 0.25f, 1.0f) // Highlight shorts in red
             : glm::vec4(0.85f, 0.85f, 0.85f, 0.95f)},
        {ssL5.str(), glm::vec4(0.75f, 0.85f, 0.95f, 0.90f)}
    };

    if (!showMetrics)
        lines.clear();
    if (metrics.evalResult != EvalOrderResult::OK)
    {
        const glm::vec4 errorColor(1.0f, 0.25f, 0.25f, 1.0f);
        const std::string error = metrics.evalResult == EvalOrderResult::CYCLE_DETECTED
                                      ? "[SIMULATION PAUSED] Feedback loop"
                                      : "[SIMULATION PAUSED] Connection rejected";
        lines.insert(lines.begin(), {error, errorColor});
        lines.insert(lines.begin() + 1, {"Fix the wiring to resume.", errorColor});
    }


    std::vector<TextRun> runs;
    constexpr float scale = 0.35f;
    for (std::size_t i = 0; i < lines.size(); ++i)
        runs.push_back(
            {lines[i].first,
             {static_cast<float>(width) - 16 - getTextWidth(lines[i].first, scale, font),
              24 + static_cast<float>(i) * 20},
             scale,
             lines[i].second}
        );
    return runs;
}
