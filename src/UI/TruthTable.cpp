#include "UI/TruthTable.h"

#include "Components/FlipFlop.h"
#include "Components/Gate.h"
#include "Components/Latch.h"

#include <algorithm>
#include <utility>

namespace
{
std::string pinName(const std::vector<PinUI>& pins, int index, const char* fallback)
{
    const auto found = std::find_if(
        pins.begin(),
        pins.end(),
        [=](const PinUI& pin) { return pin.pin_index == static_cast<unsigned>(index); }
    );
    return found != pins.end() && !found->label.empty()
               ? found->label
               : std::string(fallback) + std::to_string(index + 1);
}
} // namespace

bool hasTruthTable(const Component* component)
{
    return dynamic_cast<const Gate*>(component) || dynamic_cast<const Latch*>(component) ||
           dynamic_cast<const FlipFlop*>(component);
}

TruthTable buildTruthTable(
    const Component& component, const std::vector<PinUI>& inputs, const std::vector<PinUI>& outputs
)
{
    TruthTable table;
    if (!hasTruthTable(&component))
        return table;
    const auto* gate = dynamic_cast<const Gate*>(&component);
    const auto* latch = dynamic_cast<const Latch*>(&component);
    const auto* flip = dynamic_cast<const FlipFlop*>(&component);
    table.inputCount = flip ? flip->clockInputPin() : component.getInputPinCount();
    if (table.inputCount > 8)
    {
        table.notes = {"Tables support up to 8 inputs.", "This component has more inputs."};
        return table;
    }
    bool invertedInputs = false;
    for (int i = 0; i < table.inputCount; ++i)
    {
        auto name = pinName(inputs, i, "In");
        if (gate && gate->isInputInverted(i))
        {
            name += "*";
            invertedInputs = true;
        }
        table.headers.push_back(std::move(name));
    }
    for (int i = 0; i < component.getOutputPinCount(); ++i)
        table.headers.push_back(pinName(outputs, i, "Out") + (gate ? "" : " next"));
    if (gate)
        table.notes = {
            "Inputs are external pin levels.",
            invertedInputs ? "* = inverted input; output includes inversion." : "0 = LOW; 1 = HIGH."
        };
    else
        table.notes = {
            flip ? "At CLK 0 -> 1; otherwise hold." : "Level-sensitive: evaluates while enabled.",
            "Q / ~Q in results = stored values."
        };
    if (latch && latch->getType() == LatchType::SR_LATCH)
        table.notes[0] = "S=R=1 is invalid: both outputs LOW.";

    // Explicit next-state tables keep hold/toggle semantics visible to learners.
    if (latch)
    {
        if (latch->getType() == LatchType::SR_LATCH)
            table.rows = {
                {"0", "0", "Q", "~Q"},
                {"0", "1", "0", "1"},
                {"1", "0", "1", "0"},
                {"1", "1", "0", "0"}
            };
        else
            table.rows = {
                {"0", "0", "Q", "~Q"},
                {"0", "1", "0", "1"},
                {"1", "0", "Q", "~Q"},
                {"1", "1", "1", "0"}
            };
        return table;
    }
    if (flip)
    {
        switch (flip->getType())
        {
        case FlipFlopType::D:
            table.rows = {{"0", "0", "1"}, {"1", "1", "0"}};
            break;
        case FlipFlopType::T:
            table.rows = {{"0", "Q", "~Q"}, {"1", "~Q", "Q"}};
            break;
        case FlipFlopType::JK:
            table.rows = {
                {"0", "0", "Q", "~Q"},
                {"0", "1", "0", "1"},
                {"1", "0", "1", "0"},
                {"1", "1", "~Q", "Q"}
            };
            break;
        }
        return table;
    }

    // Evaluate an isolated gate; never change live inputs or simulation state.
    auto copy = component.clone();
    for (int combination = 0; combination < (1 << table.inputCount); ++combination)
    {
        std::vector<std::string> row;
        for (int i = 0; i < table.inputCount; ++i)
        {
            const bool high = (combination >> (table.inputCount - 1 - i)) & 1;
            row.push_back(high ? "1" : "0");
            copy->setStateInPin(i, high);
        }
        copy->evaluate();
        row.push_back(copy->getStateOutPin(0) ? "1" : "0");
        table.rows.push_back(std::move(row));
    }
    return table;
}
