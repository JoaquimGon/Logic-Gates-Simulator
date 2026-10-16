#include "Components/Gate.h"
#include "Editor/Scene.h"
#include "UI/UI.h"

#include <algorithm>
#include <cmath>
#include <limits>


/** @brief Map a component information row to its concrete input or output pin. */
const PinUI* UI::infoPinAtLine(const Scene& scene, int line) const
{
    const auto* view = scene.getCommittedComponentView(m_infoComponent);
    if (!view)
        return nullptr;
    const auto info = componentInfo(scene);
    const int extra = !canName(scene) && !view->getBodyLabel().empty() && view->getBodyLabel() != info.front();
    int start = 2 + extra;
    for (const auto* pins : {&view->getInputPins(), &view->getOutputPins()})
    {
        if (line >= start && line < start + static_cast<int>(pins->size()))
            return &(*pins)[line - start];
        start += static_cast<int>(pins->size()) + 1;
    }
    return nullptr;
}


/** @brief Build a stable nested signal reference for a selected information-panel pin. */
std::optional<SignalTrace> UI::signalForPin(const Scene& scene, PinType direction, unsigned int index) const
{
    const auto* view = scene.getCommittedComponentView(m_infoComponent);
    if (!view)
        return std::nullopt;
    const auto& pins = direction == PinType::INPUT ? view->getInputPins() : view->getOutputPins();
    for (const auto& pin : pins)
        if (pin.pin_index == index)
        {
            auto label = scene.signalName(m_infoComponent, direction, index);
            if (m_views && m_views->inspecting())
                label = m_views->inspectionLabel() + " / " + label;
            return SignalTrace{m_views ? m_views->signalPath(m_infoComponent) : ClockPath{m_infoComponent},
                               direction,
                               index,
                               pin.id,
                               label,
                               {}};
        }
    return std::nullopt;
}


CanvasViewport UI::signalTraceBounds(const Scene& scene, PinType direction, unsigned int index) const
{
    if (m_infoComponent == -1 || m_truthTableOpen)
        return {};
    const auto bounds = infoBounds(scene);
    const auto* gate = infoGate(scene);
    const int rows = infoVisibleRows(scene), inputRows = gateInputRows(scene);
    const int offset = gate ? gateInputScroll(scene) : m_infoScroll;
    for (int row = 0; row < rows; ++row)
    {
        const bool inputRow = gate && row >= 1 && row <= inputRows;
        const int line = !gate      ? row + offset + 1
                         : row == 0 ? 1
                         : inputRow ? row + offset + 1
                                    : row - inputRows + gate->getInputPinCount() + 1;
        const auto* pin = infoPinAtLine(scene, line);
        if (pin && pin->type == direction && pin->pin_index == index)
            return {
                bounds.x + bounds.width - 62,
                bounds.y + infoHeaderSize(scene) + infoLabelHeight + (canName(scene) ? infoNameHeight : 0) +
                    infoSettingsHeight(scene) + row * infoRowHeight + 1,
                50, infoRowHeight - 2
            };
    }
    return {};
}


CanvasViewport UI::signalRemoveBounds(int row) const
{
    const auto list = clockListBounds();
    if (row * 48 >= list.height)
        return {};
    return {list.x + clockLabelWidth() - 28, list.y + row * 48 + 23, 24, 21};
}


/** @brief Choose an explicit fitted signal interval from observed transitions rather than unrecorded predictions. */
double UI::fittedSignalWindow() const
{
    double shortest = std::numeric_limits<double>::infinity();
    for (const auto& signal : clockTimeline().signals())
        if (signal.shortestInterval > 0)
            shortest = std::min(shortest, signal.shortestInterval);
    if (!std::isfinite(shortest))
        return m_signalWindow;
    const double duration = std::clamp(shortest * 8, 0.000001, 3600.0);
    return std::clamp(std::exp2(std::ceil(std::log2(duration))), 0.000001, 3600.0);
}
