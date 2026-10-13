#include "Editor/CircuitViews.h"
#include "Editor/Input.h"
#include "Simulation/Subcircuit.h"

#include <utility>

std::string CircuitViews::inspectionLabel() const
{
    std::string label;
    for (const auto& inspection : m_inspections)
    {
        if (!label.empty())
            label += " / ";
        label += inspection.name;
    }
    return label;
}

bool CircuitViews::inspectComponent(int componentId, Input& input)
{
    if (inspecting() && !refreshInspection(input, false))
        return false;
    const auto& parent = std::as_const(*this).displayedScene();
    const auto* sub = dynamic_cast<const Subcircuit*>(parent.getLogicComponent(componentId));
    const auto* view = parent.getCommittedComponentView(componentId);
    if (!sub || !sub->authoredScene() || sub->componentIds().empty() || !view)
        return false;
    const auto* definition = parent.getComponentCatalog().find(view->getDefinitionIdentity().id);
    std::string name = view->getBodyLabel();
    if (name.empty() || (definition && name == definition->displayName))
    {
        int ordinal = 0;
        for (const auto& [id, candidate] : parent.getComponentViewMap())
            if (id <= componentId &&
                candidate->getDefinitionIdentity().id == view->getDefinitionIdentity().id)
                ++ordinal;
        name = (definition ? definition->displayName : "Circuit") + " " + std::to_string(ordinal);
    }
    auto layout = std::make_unique<Scene>(*sub->authoredScene());
    layout->observeRuntime(
        sub->runtimeCircuit(),
        sub->componentIds(),
        activeScene().getLastEvalResult(),
        activeScene().isSimulationDirty()
    );
    const int runtimeId = parent.runtimeComponentId(componentId);
    m_inspectedRevision = activeScene().getRevision();
    m_inspections.push_back(
        {runtimeId, std::move(name), std::move(layout), input.getPanOffset(), input.getZoom()}
    );
    input.setScene(&displayedScene());
    input.setClockTimeline(&activeClockTimeline());
    input.setCanvasFocused(true);
    return true;
}

bool CircuitViews::leaveInspection(Input& input)
{
    if (!inspecting())
        return false;
    const auto pan = m_inspections.back().parentPan;
    const auto zoom = m_inspections.back().parentZoom;
    // Switch Input before releasing the scene it currently borrows.
    Scene* destination = m_inspections.size() > 1
                             ? m_inspections[m_inspections.size() - 2].scene.get()
                             : &activeScene();
    input.setScene(
        destination, m_inspections.size() == 1 ? m_views[m_active].history.get() : nullptr
    );
    m_inspections.pop_back();
    input.setClockTimeline(&activeClockTimeline());
    input.setPanOffset(pan);
    input.setZoom(zoom);
    input.setCanvasFocused(true);
    return true;
}

void CircuitViews::closeInspection(Input& input)
{
    while (leaveInspection(input))
    {
    }
}

bool CircuitViews::refreshInspection(Input& input, bool sync)
{
    if (!inspecting())
        return false;
    if (activeScene().getRevision() != m_inspectedRevision)
    {
        closeInspection(input);
        return false;
    }
    const Circuit* parent = nullptr;
    for (auto& inspection : m_inspections)
    {
        const auto* component =
            parent ? parent->getComponent(inspection.runtimeId)
                   : std::as_const(*this).activeScene().getLogicComponent(inspection.runtimeId);
        const auto* sub = dynamic_cast<const Subcircuit*>(component);
        if (!sub)
        {
            closeInspection(input);
            return false;
        }
        if (sync)
            inspection.scene->observeRuntime(
                sub->runtimeCircuit(),
                sub->componentIds(),
                activeScene().getLastEvalResult(),
                activeScene().isSimulationDirty()
            );
        parent = &sub->runtimeCircuit();
    }
    return true;
}

ClockPath CircuitViews::signalPath(int authoredId) const
{
    ClockPath path;
    for (const auto& inspection : m_inspections)
        path.push_back(inspection.runtimeId);
    path.push_back(displayedScene().runtimeComponentId(authoredId));
    return path;
}
