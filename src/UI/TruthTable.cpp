#include "UI/TruthTable.h"

#include "Components/Clock.h"
#include "Components/FlipFlop.h"
#include "Components/Gate.h"
#include "Components/InputPin.h"
#include "Components/Latch.h"
#include "Components/OutputPin.h"
#include "Simulation/Circuit.h"
#include "Simulation/Subcircuit.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace
{
std::string pinName(const std::vector<PinUI>& pins, int index, const char* fallback)
{
    const auto found = std::find_if(
        pins.begin(), pins.end(), [=](const PinUI& pin) { return pin.pin_index == static_cast<unsigned>(index); }
    );
    return found != pins.end() && !found->label.empty() ? found->label
                                                        : std::string(fallback) + std::to_string(index + 1);
}


/** @brief Reject time-dependent, stateful, cyclic or excessive nested circuits before truth-table evaluation. */
std::vector<std::string> combinationalProblem(const Circuit& circuit, std::size_t& remaining, int depth = 0)
{
    if (depth >= 8)
        return {"Truth table unavailable.", "Nested circuit depth exceeds the limit."};
    const auto ids = circuit.componentIds();
    std::unordered_map<int, std::size_t> incoming;
    std::vector<int> ready;
    for (int id : ids)
    {
        const auto* component = circuit.getComponent(id);
        const auto work = 1 + component->getOutConnections().size();
        if (remaining < work)
            return {"Calculation limit reached.", "Circuit is too costly for a truth table."};
        remaining -= work;
        if (dynamic_cast<const Clock*>(component))
            return {"Truth table unavailable.", "Contains clocks; may depend on time."};
        if (dynamic_cast<const Latch*>(component) || dynamic_cast<const FlipFlop*>(component))
            return {
                "Truth table unavailable.", "Contains latches or flip-flops.", "Outputs may depend on stored state."
            };
        if (const auto* nested = dynamic_cast<const Subcircuit*>(component))
        {
            const auto problem = combinationalProblem(nested->runtimeCircuit(), remaining, depth + 1);
            if (!problem.empty())
                return problem;
        }
        else if (
            !dynamic_cast<const Gate*>(component) && !dynamic_cast<const InputPin*>(component) &&
            !dynamic_cast<const OutputPin*>(component)
        )
            return {"Truth table unavailable.", "Contains unsupported component behavior."};
        incoming[id] = component->getInConnections().size();
        if (incoming[id] == 0)
            ready.push_back(id);
    }
    // This check only classifies the design; simulation keeps its event-driven propagation.
    for (std::size_t i = 0; i < ready.size(); ++i)
        for (const auto& edge : circuit.getComponent(ready[i])->getOutConnections())
            if (--incoming.at(edge.destComponentId) == 0)
                ready.push_back(edge.destComponentId);
    if (ready.size() != ids.size())
        return {"Truth table unavailable.", "Contains feedback connections.", "Outputs may depend on previous state."};
    return {};
}
} // namespace


bool hasTruthTable(const Component* component)
{
    return dynamic_cast<const Gate*>(component) || dynamic_cast<const Latch*>(component) ||
           dynamic_cast<const FlipFlop*>(component) || dynamic_cast<const Subcircuit*>(component);
}


TruthTable
buildTruthTable(const Component& component, const std::vector<PinUI>& inputs, const std::vector<PinUI>& outputs)
{
    TruthTable table;
    if (!hasTruthTable(&component))
        return table;
    const auto* gate = dynamic_cast<const Gate*>(&component);
    const auto* latch = dynamic_cast<const Latch*>(&component);
    const auto* flip = dynamic_cast<const FlipFlop*>(&component);
    const auto* sub = dynamic_cast<const Subcircuit*>(&component);
    table.inputCount = flip ? flip->clockInputPin() : component.getInputPinCount();
    if (table.inputCount > 8)
    {
        table.notes = {
            "Truth table too big.", "Maximum 8 inputs; this component has " + std::to_string(table.inputCount) + "."
        };
        return table;
    }
    // One budget covers inspection and all combinations, including nested evaluations.
    std::size_t remaining = 100000;
    if (sub)
    {
        table.notes = combinationalProblem(sub->runtimeCircuit(), remaining);
        if (!table.notes.empty())
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
        table.headers.push_back(pinName(outputs, i, "Out") + (latch || flip ? " next" : ""));
    if (gate)
        table.notes = {
            "Inputs are external pin levels.",
            invertedInputs ? "* = inverted input; output includes inversion." : "0 = LOW; 1 = HIGH."
        };
    else if (sub)
        table.notes = {"Calculated from internal circuitry.", "Inputs are external pin levels."};
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
            table.rows = {{"0", "0", "Q", "~Q"}, {"0", "1", "0", "1"}, {"1", "0", "1", "0"}, {"1", "1", "0", "0"}};
        else
            table.rows = {{"0", "0", "Q", "~Q"}, {"0", "1", "0", "1"}, {"1", "0", "Q", "~Q"}, {"1", "1", "1", "0"}};
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
            table.rows = {{"0", "0", "Q", "~Q"}, {"0", "1", "0", "1"}, {"1", "0", "1", "0"}, {"1", "1", "~Q", "Q"}};
            break;
        }
        return table;
    }

    // Evaluate an isolated gate/circuit; never change live inputs or simulation state.
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
        if (sub)
        {
            copy->evaluate(remaining);
            if (copy->simulationResult() != SimulationResult::OK)
            {
                table.rows.clear();
                table.notes = {
                    remaining == 0 ? "Calculation limit reached." : "Circuit did not settle for every input.",
                    "No reliable truth table could be calculated."
                };
                return table;
            }
        }
        else
            copy->evaluate();
        for (int i = 0; i < copy->getOutputPinCount(); ++i)
            row.push_back(copy->getStateOutPin(i) ? "1" : "0");
        table.rows.push_back(std::move(row));
    }
    return table;
}
