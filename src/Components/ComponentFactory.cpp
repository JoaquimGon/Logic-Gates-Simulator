#include "Components/ComponentFactory.h"

#include "Components/Views/BoxView.h"
#include "Components/Views/ClockView.h"
#include "Components/Views/GateView.h"
#include "Components/Views/InputPinView.h"
#include "Components/Views/LatchView.h"
#include "Components/Views/OutputPinView.h"
#include "Simulation/Subcircuit.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace
{
void validateOffset(GridCoords position, GridCoords relative)
{
    for (const auto sum :
         {static_cast<std::int64_t>(position.x) + relative.x, static_cast<std::int64_t>(position.y) + relative.y})
        if (sum < std::numeric_limits<int>::min() || sum > std::numeric_limits<int>::max())
            throw std::invalid_argument("Component pin/lead exceeds the grid coordinate range.");
}


bool hasInputInversion(const std::vector<PinUI>& pins)
{
    return std::any_of(pins.begin(), pins.end(), [](const auto& pin) { return pin.inverted; });
}


void shiftInputs(ComponentLayout& layout, int delta)
{
    auto shift = [=](GridCoords& point)
    {
        validateOffset(point, {delta, 0});
        point.x += delta;
    };
    for (auto& pin : layout.inputs)
    {
        shift(pin.relative_pos);
        for (auto& point : pin.lead)
            shift(point);
    }
}


/** @brief Convert resolved presentation geometry into the concrete pins and body used by a component view. */
ComponentLayout viewLayout(const ResolvedComponent& resolved)
{
    ComponentLayout layout{{resolved.layout.width, resolved.layout.height}, resolved.presentation.shader.key, {}, {}};
    for (const auto& pin : resolved.layout.pins)
    {
        PinUI visual{pin.direction, pin.index,   PinState::DISCONNECTED, pin.anchor, pin.id, pin.label,
                     pin.lead,      pin.inverted};
        (pin.direction == PinType::INPUT ? layout.inputs : layout.outputs).push_back(std::move(visual));
    }
    return layout;
}
} // namespace


CreatedComponent ComponentFactory::create(
    Circuit& circuit, const ComponentCatalog& catalog, std::string_view definitionId, GridCoords position,
    const ComponentOverrides& overrides, std::uint32_t version, Orientation orientation
)
{
    const auto resolved = catalog.resolve(definitionId, overrides, version);
    orientationName(orientation);
    if (orientation != Orientation::Right && !supportsOrientation(resolved.behavior))
        throw std::invalid_argument("Orientation editing requires a gate, latch, flip-flop or subcircuit.");
    auto layout = viewLayout(resolved);
    for (const auto& pin : resolved.layout.pins)
    {
        validateOffset(position, rotateGridPoint(pin.anchor, orientation));
        for (const auto point : pin.lead)
            validateOffset(position, rotateGridPoint(point, orientation));
    }
    int id;
    std::unique_ptr<ComponentView> view;
    if (const auto* gate = std::get_if<GateType>(&resolved.behavior))
    {
        id = circuit.addGate(*gate, static_cast<int>(layout.inputs.size()));
        for (const auto& pin : layout.inputs)
            static_cast<Gate*>(circuit.getComponent(id))->setInputInverted(pin.pin_index, pin.inverted);
        view = std::make_unique<GateView>(position, id, layout.size, layout.shader, layout.inputs, layout.outputs);
    }
    else if (std::holds_alternative<ManualInputBehavior>(resolved.behavior))
    {
        id = circuit.addInputPin(resolved.inputState);
        view = std::make_unique<InputPinView>(position, id, layout.size, layout.shader, layout.outputs);
    }
    else if (std::holds_alternative<ClockBehavior>(resolved.behavior))
    {
        id = circuit.addClock(resolved.clockFrequency);
        static_cast<Clock*>(circuit.getComponent(id))->setPaused(resolved.clockPaused);
        view = std::make_unique<ClockView>(position, id, layout.size, layout.shader, layout.outputs);
    }
    else if (std::holds_alternative<OutputBehavior>(resolved.behavior))
    {
        id = circuit.addOutputPin();
        view = std::make_unique<OutputPinView>(position, id, layout.size, layout.shader, layout.inputs);
    }
    else if (const auto* flip = std::get_if<FlipFlopType>(&resolved.behavior))
    {
        id = circuit.addComponent(std::make_unique<FlipFlop>(-1, *flip));
        view = std::make_unique<BoxView>(position, id, layout.size, layout.shader, layout.inputs, layout.outputs);
    }
    else if (const auto* sub = std::get_if<SubcircuitBehavior>(&resolved.behavior))
    {
        id = circuit.addComponent(std::make_unique<Subcircuit>(*sub));
        view = std::make_unique<BoxView>(position, id, layout.size, layout.shader, layout.inputs, layout.outputs);
    }
    else
    {
        id = circuit.addLatch(std::get<LatchType>(resolved.behavior));
        view = std::make_unique<LatchView>(position, id, layout.size, layout.shader, layout.inputs, layout.outputs);
    }
    view->m_definition = resolved.identity;
    view->m_bodyLabel = resolved.presentation.bodyLabel;
    view->m_showPinLabels = resolved.presentation.showPinLabels;
    view->m_showBodyLabel = resolved.presentation.showBodyLabel;
    view->m_bodyStyle = resolved.presentation.body;
    view->m_orientation = orientation;
    if (std::holds_alternative<ManualInputBehavior>(resolved.behavior) ||
        std::holds_alternative<OutputBehavior>(resolved.behavior))
    {
        static std::mt19937_64 random(std::random_device{}());
        view->m_portId = "port." + std::to_string(random());
    }
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
                throw std::invalid_argument("Visual pin lists must match their declared directions.");
            geometry.pins.push_back(
                {pin.id, pin.label, pin.type, pin.pin_index, pin.relative_pos, pin.lead, pin.inverted}
            );
        }
    overrides.layout = std::move(geometry);
    return overrides;
}


ComponentLayout ComponentFactory::validateLayout(
    const ComponentCatalog& catalog, const ComponentView& view, const ComponentLayout& layout
)
{
    auto resolved =
        catalog.resolve(view.getDefinitionIdentity().id, layoutOverrides(layout), view.getDefinitionIdentity().version);
    if (layout.shader != view.getShaderName())
        throw std::invalid_argument("Layout edits cannot change the definition's presentation.");
    resolved.presentation.shader.key = view.getShaderName();
    return viewLayout(resolved);
}


ComponentLayout
ComponentFactory::resizeGateLayout(const ComponentCatalog& catalog, const ComponentView& view, int inputCount)
{
    auto definition = *catalog.find(view.getDefinitionIdentity().id);
    const ComponentLayout current{view.getSize(), view.getShaderName(), view.getInputPins(), view.getOutputPins()};
    // Resolve a temporary copy with the instance geometry; catalog defaults never change.
    definition.layout = *layoutOverrides(current).layout;
    auto resolved = resolveDefinition(definition, {.inputCount = inputCount});
    resolved.presentation.shader.key = view.getShaderName();
    auto layout = viewLayout(resolved);
    if (hasInputInversion(current.inputs) && !hasInputInversion(layout.inputs))
        shiftInputs(layout, 1);
    return layout;
}


ComponentLayout ComponentFactory::invertedInputLayout(
    const ComponentCatalog& catalog, const ComponentView& view, unsigned int index, bool inverted
)
{
    const auto* definition = catalog.find(view.getDefinitionIdentity().id);
    if (!definition || !std::holds_alternative<GateType>(definition->behavior) ||
        definition->presentation.kind != PresentationKind::NativeSdf)
        throw std::invalid_argument("Input inversion editing requires a native logic gate.");
    ComponentLayout layout{view.getSize(), view.getShaderName(), view.getInputPins(), view.getOutputPins()};
    const bool before = hasInputInversion(layout.inputs);
    auto pin = std::find_if(
        layout.inputs.begin(), layout.inputs.end(), [=](const auto& value) { return value.pin_index == index; }
    );
    if (pin == layout.inputs.end())
        throw std::invalid_argument("Input pin no longer exists.");
    pin->inverted = inverted;
    const bool after = hasInputInversion(layout.inputs);
    if (before != after)
        shiftInputs(layout, after ? -1 : 1);
    return validateLayout(catalog, view, layout);
}


ComponentLayout
ComponentFactory::invertedGateLayout(const ComponentCatalog& catalog, const ComponentView& view, bool inverted)
{
    auto definition = *catalog.find(view.getDefinitionIdentity().id);
    definition.behavior =
        Gate::typeWithInversion(std::get<GateType>(definition.behavior), view.getBodyStyle().inverted);
    definition.presentation.body = view.getBodyStyle();
    definition.presentation.shader.key = view.getShaderName();
    definition.layout =
        *layoutOverrides({view.getSize(), view.getShaderName(), view.getInputPins(), view.getOutputPins()}).layout;
    return viewLayout(resolveDefinition(definition, {.inverted = inverted}));
}


void ComponentFactory::validatePosition(const ComponentView& view, GridCoords position)
{
    for (const auto* pins : {&view.getInputPins(), &view.getOutputPins()})
        for (const auto& pin : *pins)
        {
            validateOffset(position, rotateGridPoint(pin.relative_pos, view.getOrientation()));
            for (const auto point : pin.lead)
                validateOffset(position, rotateGridPoint(point, view.getOrientation()));
        }
}
