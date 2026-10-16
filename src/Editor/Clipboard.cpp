#include "Editor/Clipboard.h"

#include "Components/ComponentFactory.h"
#include "Editor/Gestures/Selection.h"
#include "Editor/Scene.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>


bool EditorClipboard::copy(const Scene& scene, const Selection& selection)
{
    EditorClipboard copied;
    std::set<std::string> definitions;
    for (int id : selection.components())
    {
        const auto* view = scene.getCommittedComponentView(id);
        const auto* logic = scene.getLogicComponent(id);
        if (!view || !logic)
            continue;
        const auto& identity = view->getDefinitionIdentity();
        const auto* definition = scene.getComponentCatalog().find(identity.id);
        if (!definition)
            continue;
        auto options = ComponentFactory::layoutOverrides(
            {view->getSize(), view->getShaderName(), view->getInputPins(), view->getOutputPins()}
        );
        if (const auto* gate = dynamic_cast<const Gate*>(logic))
        {
            if (gate->getType() != NOT && definition->presentation.kind == PresentationKind::NativeSdf)
                options.inverted = gate->isInverted();
        }
        else if (const auto* clock = dynamic_cast<const Clock*>(logic))
        {
            options.clockFrequency = clock->getFrequency();
            options.clockPaused = clock->isPaused();
        }
        else if (const auto* input = dynamic_cast<const InputPin*>(logic))
            options.inputState = input->getState();
        copied.m_components.push_back(
            {identity.id, view->getGridPosition(), std::move(options), PlacementPolicy::RejectSharedAnchors,
             identity.version, view->getBodyLabel(), view->showsBodyLabel(), view->getOrientation()}
        );
        if (!identity.id.starts_with("native.") && definitions.insert(identity.id).second)
            copied.m_definitions.push_back(*definition);
    }
    for (WireId id : selection.wires())
        if (const auto* wire = scene.getWire(id))
            copied.m_wires.push_back(wire->getPath());
    // Ordinary wire clicks select a segment; Ctrl-click and box selection select whole sections.
    if (selection.segment())
        copied.m_wires.push_back({selection.segment()->start, selection.segment()->end});
    if (copied.m_components.empty() && copied.m_wires.empty())
        return false; // An empty selection leaves the previous clipboard available.

    GridCoords low{std::numeric_limits<int>::max(), std::numeric_limits<int>::max()};
    GridCoords high{std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
    auto include = [&](GridCoords point)
    {
        low.x = std::min(low.x, point.x);
        low.y = std::min(low.y, point.y);
        high.x = std::max(high.x, point.x);
        high.y = std::max(high.y, point.y);
    };
    for (const auto& component : copied.m_components)
        include(component.position);
    for (const auto& wire : copied.m_wires)
        for (const auto point : wire)
            include(point);
    copied.m_center = {
        static_cast<int>((static_cast<std::int64_t>(low.x) + high.x) / 2),
        static_cast<int>((static_cast<std::int64_t>(low.y) + high.y) / 2)
    };
    *this = std::move(copied);
    return true;
}


EditBatch EditorClipboard::pasteBatch(const Scene& destination, GridCoords center) const
{
    EditBatch batch;
    for (const auto& definition : m_definitions)
    {
        const auto* existing = destination.getComponentCatalog().find(definition.identity.id);
        if (!existing)
            batch.push_back(RegisterComponentDefinition{definition});
        else if (existing->identity.version != definition.identity.version)
            throw std::invalid_argument("Copied component definition has changed; copy it again.");
    }
    auto translate = [&](GridCoords point)
    {
        const auto x = static_cast<std::int64_t>(point.x) + center.x - m_center.x;
        const auto y = static_cast<std::int64_t>(point.y) + center.y - m_center.y;
        if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
            y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
            throw std::invalid_argument("Paste would exceed the grid coordinate range.");
        return GridCoords{static_cast<int>(x), static_cast<int>(y)};
    };
    for (auto component : m_components)
    {
        component.position = translate(component.position);
        batch.push_back(std::move(component));
    }
    for (auto path : m_wires)
    {
        for (auto& point : path)
            point = translate(point);
        batch.push_back(AddWire{std::move(path)});
    }
    return batch;
}
