#include "Components/ComponentFactory.h"

#include "Components/Views/ClockView.h"
#include "Components/Views/GateView.h"
#include "Components/Views/InputPinView.h"
#include "Components/Views/LatchView.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
void validateOffset(GridCoords position, GridCoords relative)
{
    for (const auto sum :
         {static_cast<std::int64_t>(position.x) + relative.x,
          static_cast<std::int64_t>(position.y) + relative.y})
        if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
            throw std::invalid_argument("Component pin/lead exceeds the grid coordinate range.");
}

ComponentLayout viewLayout(const ResolvedComponent& resolved)
{
    ComponentLayout layout{
        {resolved.layout.width, resolved.layout.height}, resolved.presentation.shader.key, {}, {}
    };
    for (const auto& pin : resolved.layout.pins)
    {
        PinUI visual{
            pin.direction,
            pin.index,
            PinState::DISCONNECTED,
            pin.anchor,
            pin.id,
            pin.label,
            pin.lead
        };
        (pin.direction == PinType::INPUT ? layout.inputs : layout.outputs)
            .push_back(std::move(visual));
    }
    return layout;
}
} // namespace

CreatedComponent ComponentFactory::create(
    Circuit& circuit,
    const ComponentCatalog& catalog,
    std::string_view definitionId,
    GridCoords position,
    const ComponentOverrides& overrides,
    std::uint32_t version
)
{
    const auto resolved = catalog.resolve(definitionId, overrides, version);
    auto layout = viewLayout(resolved);
    for (const auto& pin : resolved.layout.pins)
    {
        validateOffset(position, pin.anchor);
        for (const auto point : pin.lead)
            validateOffset(position, point);
    }
    int id;
    std::unique_ptr<ComponentView> view;
    if (const auto* gate = std::get_if<GateType>(&resolved.behavior))
    {
        id = circuit.addGate(*gate, static_cast<int>(layout.inputs.size()));
        view = std::make_unique<GateView>(
            position, id, layout.size, layout.shader, layout.inputs, layout.outputs
        );
    }
    else if (std::holds_alternative<ManualInputBehavior>(resolved.behavior))
    {
        id = circuit.addInputPin(resolved.inputState);
        view = std::make_unique<InputPinView>(
            position, id, layout.size, layout.shader, layout.outputs
        );
    }
    else if (std::holds_alternative<ClockBehavior>(resolved.behavior))
    {
        id = circuit.addClock(resolved.clockFrequency);
        static_cast<Clock*>(circuit.getComponent(id))->setPaused(resolved.clockPaused);
        view =
            std::make_unique<ClockView>(position, id, layout.size, layout.shader, layout.outputs);
    }
    else
    {
        id = circuit.addLatch(std::get<LatchType>(resolved.behavior));
        view = std::make_unique<LatchView>(
            position, id, layout.size, layout.shader, layout.inputs, layout.outputs
        );
    }
    view->m_definition = resolved.identity;
    view->m_bodyLabel = resolved.presentation.bodyLabel;
    view->m_showPinLabels = resolved.presentation.showPinLabels;
    view->m_bodyStyle = resolved.presentation.body;
    return {id, std::move(view)};
}

ComponentOverrides ComponentFactory::layoutOverrides(const ComponentLayout& layout)
{
    ComponentOverrides overrides;
    DefinitionLayout geometry{layout.size.x, layout.size.y, {}};
    for (const auto* pins : {&layout.inputs, &layout.outputs})
        for (const auto& pin : *pins)
        {
            if (pin.type != (pins == &layout.inputs ? PinType::INPUT : PinType::OUTPUT))
                throw std::invalid_argument(
                    "Visual pin lists must match their declared directions."
                );
            geometry.pins.push_back(
                {pin.id, pin.label, pin.type, pin.pin_index, pin.relative_pos, pin.lead}
            );
        }
    overrides.layout = std::move(geometry);
    return overrides;
}

ComponentLayout ComponentFactory::validateLayout(
    const ComponentCatalog& catalog, const ComponentView& view, const ComponentLayout& layout
)
{
    auto resolved = catalog.resolve(
        view.getDefinitionIdentity().id,
        layoutOverrides(layout),
        view.getDefinitionIdentity().version
    );
    if (layout.shader != view.getShaderName())
        throw std::invalid_argument("Layout edits cannot change the definition's presentation.");
    resolved.presentation.shader.key = view.getShaderName();
    return viewLayout(resolved);
}

ComponentLayout ComponentFactory::resizeGateLayout(
    const ComponentCatalog& catalog, const ComponentView& view, int inputCount
)
{
    auto definition = *catalog.find(view.getDefinitionIdentity().id);
    const ComponentLayout current{
        view.getSize(), view.getShaderName(), view.getInputPins(), view.getOutputPins()
    };
    // Resolve a temporary copy with the instance geometry; catalog defaults never change.
    definition.layout = *layoutOverrides(current).layout;
    auto resolved = resolveDefinition(definition, {.inputCount = inputCount});
    resolved.presentation.shader.key = view.getShaderName();
    return viewLayout(resolved);
}

void ComponentFactory::validatePosition(const ComponentView& view, GridCoords position)
{
    for (const auto* pins : {&view.getInputPins(), &view.getOutputPins()})
        for (const auto& pin : *pins)
        {
            validateOffset(position, pin.relative_pos);
            for (const auto point : pin.lead)
                validateOffset(position, point);
        }
}
