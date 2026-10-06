#include "Components/Definitions/ComponentDefinition.h"

#include "Components/Definitions/NativeDefinitions.h"
#include "Geometry/GridMetrics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace
{
constexpr int maxPins = 256;

int countPins(const DefinitionLayout& layout, PinType direction)
{
    return static_cast<int>(std::count_if(
        layout.pins.begin(),
        layout.pins.end(),
        [=](const auto& pin) { return pin.direction == direction; }
    ));
}

void validateLayout(
    const ComponentBehavior& behavior, const DefinitionLayout& layout, bool requireIdentity = true
)
{
    if (!std::isfinite(layout.width) || !std::isfinite(layout.height) || layout.width <= 0 ||
        layout.height <= 0 || layout.pins.size() > maxPins)
        throw std::invalid_argument(
            "Positive finite dimensions and at most 256 pins are required."
        );
    std::set<std::string> identities;
    std::set<std::pair<int, int>> anchors;
    std::set<unsigned int> inputs, outputs;
    for (const auto& pin : layout.pins)
    {
        if ((requireIdentity && pin.id.empty()) ||
            (!pin.id.empty() && !identities.insert(pin.id).second) ||
            !anchors.emplace(pin.anchor.x, pin.anchor.y).second)
            throw std::invalid_argument("Pins require distinct IDs and anchors.");
        if (!pin.lead.empty())
        {
            if (pin.lead.size() < 2 || pin.lead.size() > 256 || pin.lead.back() != pin.anchor)
                throw std::invalid_argument(
                    "A lead needs 2-256 relative points ending at its pin anchor."
                );
            for (std::size_t i = 1; i < pin.lead.size(); ++i)
            {
                const auto a = pin.lead[i - 1], b = pin.lead[i];
                if (a == b || (a.x != b.x && a.y != b.y))
                    throw std::invalid_argument("Lead segments must be nonzero and orthogonal.");
            }
        }
        auto* indices = pin.direction == PinType::INPUT    ? &inputs
                        : pin.direction == PinType::OUTPUT ? &outputs
                                                           : nullptr;
        if (!indices || !indices->insert(pin.index).second)
            throw std::invalid_argument("Pin directions and indices must be valid and unique.");
    }
    for (const auto* indices : {&inputs, &outputs})
    {
        unsigned int expected = 0;
        for (unsigned int index : *indices)
            if (index != expected++)
                throw std::invalid_argument(
                    "Directional pin indices must be contiguous from zero."
                );
    }
    if (const auto* gate = std::get_if<GateType>(&behavior))
    {
        Gate check(-1, *gate, static_cast<int>(inputs.size()));
        if (outputs.size() != 1)
            throw std::invalid_argument("Native gates require one output.");
    }
    else if (const auto* latch = std::get_if<LatchType>(&behavior))
    {
        Latch check(-1, *latch);
        if (inputs.size() != 2 || outputs.size() != 2)
            throw std::invalid_argument("Native latches require two inputs and two outputs.");
    }
    else if (std::holds_alternative<OutputBehavior>(behavior))
    {
        if (inputs.size() != 1 || !outputs.empty())
            throw std::invalid_argument("Outputs require one input and no outgoing pins.");
    }
    else if (const auto* sub = std::get_if<SubcircuitBehavior>(&behavior))
    {
        if (!sub->authored || !sub->circuit || inputs.empty() || outputs.empty() ||
            inputs.size() != sub->inputs.size() || outputs.size() != sub->outputs.size())
            throw std::invalid_argument(
                "Subcircuits require a design and matching input/output ports."
            );
    }
    else if (!inputs.empty() || outputs.size() != 1)
        throw std::invalid_argument("Native sources require no inputs and one output.");
}
} // namespace

void validateClockFrequency(float hz)
{
    if (!std::isfinite(hz) || hz < 0.1f)
        throw std::invalid_argument("Clock frequency must be finite and at least 0.1 Hz.");
}

void validateDefinition(const ComponentDefinition& definition)
{
    if (definition.identity.id.empty() || definition.identity.version == 0 ||
        definition.displayName.empty())
        throw std::invalid_argument(
            "A definition requires a stable ID, nonzero version and display name."
        );
    validateLayout(definition.behavior, definition.layout);
    if (definition.pinLayoutRule != PinLayoutRule::Fixed &&
        definition.pinLayoutRule != PinLayoutRule::SymmetricGateInputs)
        throw std::invalid_argument("Unsupported pin layout rule.");
    if (definition.pinLayoutRule == PinLayoutRule::SymmetricGateInputs)
    {
        const auto* gate = std::get_if<GateType>(&definition.behavior);
        if (!gate || *gate == NOT)
            throw std::invalid_argument("Expandable symmetric inputs require a multi-input gate.");
        // Generated identities are predictable across arity edits and definition versions.
        for (const auto& pin : definition.layout.pins)
            if (pin.direction == PinType::INPUT && pin.id != "in." + std::to_string(pin.index))
                throw std::invalid_argument("Symmetric gate input IDs must use in.<index>.");
    }
    const auto& body = definition.presentation.body;
    if (body.contour < BodyContour::Box || body.contour > BodyContour::Output ||
        (body.inverted && body.contour != BodyContour::And && body.contour != BodyContour::Or &&
         body.contour != BodyContour::Xor))
        throw std::invalid_argument("Invalid body contour/inversion combination.");
    for (float channel : body.tint)
        if (!std::isfinite(channel) || channel < 0 || channel > 1)
            throw std::invalid_argument(
                "Body tint channels must be finite values from zero to one."
            );
    if (definition.presentation.kind == PresentationKind::Box)
    {
        if (body.contour != BodyContour::Box || body.inverted)
            throw std::invalid_argument("Box presentation requires the box contour.");
        const auto& box = boxShaderResources();
        const auto& resources = definition.presentation.shader;
        if (resources.key != box.key || resources.vertexPath != box.vertexPath ||
            resources.fragmentPath != box.fragmentPath)
            throw std::invalid_argument(
                "Box definitions must use the registered box presentation."
            );
    }
    else if (
        definition.presentation.kind != PresentationKind::NativeSdf ||
        definition.presentation.shader.key.empty() ||
        definition.presentation.shader.vertexPath.empty() ||
        definition.presentation.shader.fragmentPath.empty()
    )
        throw std::invalid_argument("Native presentation requires registered shader resources.");
    if (std::holds_alternative<ClockBehavior>(definition.behavior))
        validateClockFrequency(definition.defaultClockFrequency);
    else if (definition.defaultClockFrequency != 1.0f || definition.defaultClockPaused)
        throw std::invalid_argument("Clock defaults require clock behavior.");
    if (!std::holds_alternative<ManualInputBehavior>(definition.behavior) &&
        definition.defaultInputState)
        throw std::invalid_argument("Manual input defaults require manual input behavior.");
}

ResolvedComponent
resolveDefinition(const ComponentDefinition& definition, const ComponentOverrides& overrides)
{
    validateDefinition(definition);
    const bool isGate = std::holds_alternative<GateType>(definition.behavior);
    const bool isInput = std::holds_alternative<ManualInputBehavior>(definition.behavior);
    const bool isClock = std::holds_alternative<ClockBehavior>(definition.behavior);
    if ((overrides.inputCount && !isGate) || (overrides.inputState && !isInput) ||
        ((overrides.clockFrequency || overrides.clockPaused) && !isClock))
        throw std::invalid_argument("Override is not supported by this component behavior.");
    ResolvedComponent result{
        definition.identity,
        definition.behavior,
        definition.layout,
        definition.presentation,
        overrides.inputState.value_or(definition.defaultInputState),
        overrides.clockFrequency.value_or(definition.defaultClockFrequency),
        overrides.clockPaused.value_or(definition.defaultClockPaused)
    };
    const int defaultInputs = countPins(definition.layout, PinType::INPUT);
    int inputCount = overrides.inputCount.value_or(defaultInputs);
    if (overrides.layout && !overrides.inputCount)
        inputCount = countPins(*overrides.layout, PinType::INPUT);
    if (inputCount != defaultInputs)
    {
        if (definition.pinLayoutRule != PinLayoutRule::SymmetricGateInputs || inputCount < 2 ||
            inputCount >= maxPins)
            throw std::invalid_argument("This interface cannot use the requested input count.");
        const auto first = std::find_if(
            result.layout.pins.begin(),
            result.layout.pins.end(),
            [](const auto& pin) { return pin.direction == PinType::INPUT && pin.index == 0; }
        );
        const int x = first->anchor.x;
        std::erase_if(
            result.layout.pins, [](const auto& pin) { return pin.direction == PinType::INPUT; }
        );
        for (int index = 0; index < inputCount; ++index)
        {
            const auto original = std::find_if(
                definition.layout.pins.begin(),
                definition.layout.pins.end(),
                [=](const auto& pin)
                {
                    return pin.direction == PinType::INPUT &&
                           pin.index == static_cast<unsigned int>(index);
                }
            );
            PinDefinition generated{
                "in." + std::to_string(index),
                original != definition.layout.pins.end() ? original->label
                                                         : "In " + std::to_string(index),
                PinType::INPUT,
                static_cast<unsigned int>(index),
                {x, inputCount - 1 - 2 * index},
                {}
            };
            if (original != definition.layout.pins.end())
                for (const auto point : original->lead)
                {
                    const auto leadX = static_cast<std::int64_t>(point.x) + generated.anchor.x -
                                       original->anchor.x;
                    const auto leadY = static_cast<std::int64_t>(point.y) + generated.anchor.y -
                                       original->anchor.y;
                    for (const auto coordinate : {leadX, leadY})
                        if (coordinate < std::numeric_limits<int>::min() ||
                            coordinate > std::numeric_limits<int>::max())
                            throw std::invalid_argument(
                                "Generated lead exceeds the grid coordinate range."
                            );
                    generated.lead.push_back({static_cast<int>(leadX), static_cast<int>(leadY)});
                }
            result.layout.pins.push_back(std::move(generated));
        }
        result.layout.height =
            std::max(result.layout.height, 2 * GridMetrics::Spacing * inputCount);
    }
    if (!definition.presentation.allowResize && (result.layout.width != definition.layout.width ||
                                                 result.layout.height != definition.layout.height))
        throw std::invalid_argument(
            "This definition cannot resize to fit the requested input count."
        );
    if (overrides.layout)
    {
        validateLayout(definition.behavior, *overrides.layout, false);
        if (!definition.presentation.allowResize &&
            (overrides.layout->width != definition.layout.width ||
             overrides.layout->height != definition.layout.height))
            throw std::invalid_argument("This definition does not allow body resizing.");
        if (countPins(*overrides.layout, PinType::INPUT) != inputCount ||
            countPins(*overrides.layout, PinType::OUTPUT) !=
                countPins(result.layout, PinType::OUTPUT))
            throw std::invalid_argument("Layout and logical pin counts disagree.");
        auto layout = *overrides.layout;
        for (auto& pin : layout.pins)
        {
            const auto expected = std::find_if(
                result.layout.pins.begin(),
                result.layout.pins.end(),
                [&](const auto& canonical)
                { return canonical.direction == pin.direction && canonical.index == pin.index; }
            );
            if (expected == result.layout.pins.end() ||
                (!pin.id.empty() && pin.id != expected->id) ||
                (!pin.label.empty() && pin.label != expected->label))
                throw std::invalid_argument(
                    "Instance layouts cannot rename or reassign stable definition pins."
                );
            pin.id = expected->id;
            pin.label = expected->label;
        }
        result.layout = std::move(layout);
    }
    if (isClock)
        validateClockFrequency(result.clockFrequency);
    validateLayout(result.behavior, result.layout);
    return result;
}
