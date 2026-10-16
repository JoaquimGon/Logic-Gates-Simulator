#include "Editor/Scene.h"
#include "UI/UI.h"

#include <algorithm>


bool UI::hasTruthTable(const Scene& scene) const
{
    return ::hasTruthTable(scene.getLogicComponent(m_infoComponent));
}


double UI::infoHeaderSize(const Scene& scene) const
{
    return infoHeaderHeight + (hasTruthTable(scene) ? 32 : 0);
}


CanvasViewport UI::infoTabBounds(const Scene& scene, bool table) const
{
    if (!hasTruthTable(scene))
        return {};
    const auto popup = infoBounds(scene);
    const double width = (popup.width - 24) / 2;
    return {popup.x + 10 + (table ? width + 4 : 0), popup.y + infoHeaderHeight, width, 26};
}


const TruthTable& UI::truthTable(const Scene& scene) const
{
    if (m_truthComponent != m_infoComponent || m_truthRevision != scene.getRevision())
    {
        m_truthTable = {};
        const auto* logic = scene.getLogicComponent(m_infoComponent);
        const auto* view = scene.getCommittedComponentView(m_infoComponent);
        if (logic && view)
            m_truthTable = buildTruthTable(*logic, view->getInputPins(), view->getOutputPins());
        m_truthComponent = m_infoComponent;
        m_truthRevision = scene.getRevision();
    }
    return m_truthTable;
}


std::optional<int> UI::truthTableCurrentRow(const Scene& scene) const
{
    const auto& table = truthTable(scene);
    if (table.rows.empty() || scene.isSimulationDirty() || scene.getLastEvalResult() != SimulationResult::OK)
        return std::nullopt;
    const auto* view = scene.getCommittedComponentView(m_infoComponent);
    if (!view)
        return std::nullopt;
    const auto& pins = view->getInputPins();
    if (std::any_of(pins.begin(), pins.end(), [](const PinUI& pin) { return pin.state == PinState::DISCONNECTED; }))
        return std::nullopt;
    int combination = 0;
    for (int i = 0; i < table.inputCount; ++i)
    {
        const auto pin = std::find_if(
            pins.begin(), pins.end(), [=](const PinUI& value) { return value.pin_index == static_cast<unsigned>(i); }
        );
        if (pin == pins.end())
            return std::nullopt;
        combination = combination * 2 + (pin->state == PinState::ON);
    }
    return combination;
}


CanvasViewport UI::truthTableRowsBounds(const Scene& scene) const
{
    if (!m_truthTableOpen || !hasTruthTable(scene))
        return {};
    const auto popup = infoBounds(scene);
    const double top = popup.y + infoHeaderSize(scene) + truthTable(scene).notes.size() * 20 + 48 + infoRowHeight;
    return {popup.x + 10, top, popup.width - 20, std::max(0.0, popup.y + popup.height - infoFooterHeight - top)};
}


int UI::truthTableVisibleRows(const Scene& scene) const
{
    return static_cast<int>(truthTableRowsBounds(scene).height / infoRowHeight);
}


int UI::truthTableScroll(const Scene& scene) const
{
    return std::clamp(
        m_truthScroll, 0,
        std::max(0, static_cast<int>(truthTable(scene).rows.size()) - std::max(1, truthTableVisibleRows(scene)))
    );
}


CanvasViewport UI::currentTruthRowBounds(const Scene& scene) const
{
    if (!m_truthTableOpen || truthTable(scene).rows.empty())
        return {};
    const auto popup = infoBounds(scene);
    return {popup.x + popup.width - 102, popup.y + popup.height - infoFooterHeight + 2, 92, 26};
}


void UI::revealCurrentTruthRow(const Scene& scene)
{
    if (const auto row = truthTableCurrentRow(scene))
        m_truthScroll = std::max(0, *row - truthTableVisibleRows(scene) / 2);
    m_truthScroll = truthTableScroll(scene);
}
