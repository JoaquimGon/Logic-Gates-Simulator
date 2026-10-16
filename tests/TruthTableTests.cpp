#include "Components/FlipFlop.h"
#include "Components/Gate.h"
#include "Components/InputPin.h"
#include "Components/Latch.h"
#include "Simulation/Subcircuit.h"
#include "UI/TruthTable.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}


/** @brief Verify gate tables reflect external levels, configured arity, inversion, labels and the eight-input limit. */
void gates()
{
    for (GateType type : {NOT, AND, NAND, OR, NOR, XOR, NXOR})
        for (int count = type == NOT ? 1 : 2; count <= (type == NOT ? 1 : 8); ++count)
            for (int inversion : {0, (1 << count) - 1, 0x55 & ((1 << count) - 1)})
            {
                Gate gate(7, type, count);
                for (int i = 0; i < count; ++i)
                {
                    gate.setInputInverted(i, (inversion >> (count - 1 - i)) & 1);
                    gate.setStateInPin(i, i % 2 == 0);
                }
                gate.evaluate();
                const auto originalInputs = gate.getStateInPins();
                const bool originalOutput = gate.getStateOutPin();
                const auto table = buildTruthTable(gate, {}, {});
                require(table.rows.size() == (std::size_t{1} << count), "Gate table omitted combinations.");
                for (int row = 0; row < (1 << count); ++row)
                {
                    int high = 0;
                    for (int i = 0; i < count; ++i)
                    {
                        const bool bit = (row >> (count - 1 - i)) & 1;
                        require(table.rows[row][i] == (bit ? "1" : "0"), "Input order is wrong.");
                        high += bit != gate.isInputInverted(i);
                    }
                    bool expected = type == NOT                   ? high == 0
                                    : type == AND || type == NAND ? high == count
                                    : type == OR || type == NOR   ? high != 0
                                                                  : high % 2 != 0;
                    if (type == NAND || type == NOR || type == NXOR)
                        expected = !expected;
                    require(
                        table.rows[row].back() == (expected ? "1" : "0"),
                        "Displayed gate table disagrees with its configured behavior."
                    );
                }
                require(
                    gate.getStateInPins() == originalInputs && gate.getStateOutPin() == originalOutput,
                    "Building a table changed live component state."
                );
            }
    Gate gate(0, AND, 2);
    gate.setInputInverted(0, true);
    const std::vector<PinUI> pins{
        {PinType::INPUT, 1, PinState::OFF, {}, "", "Second"}, {PinType::INPUT, 0, PinState::ON, {}, "", "First"}
    };
    const auto named = buildTruthTable(gate, pins, {{PinType::OUTPUT, 0, PinState::OFF, {}, "", "Result"}});
    require(
        named.headers == std::vector<std::string>{"First*", "Second", "Result"},
        "Table used view order instead of logical pin indices/names."
    );
    Gate large(0, AND, 256);
    require(buildTruthTable(large, {}, {}).rows.empty(), "Unbounded exponential table generation.");
    InputPin source(0, false);
    require(
        !hasTruthTable(&source) && buildTruthTable(source, {}, {}).rows.empty(),
        "Sources should not advertise gate truth tables."
    );
}


bool resolve(const std::string& value, bool q, bool notQ)
{
    return value == "Q" ? q : value == "~Q" ? notQ : value == "1";
}


/** @brief Check explicit next-state rows against latch or flip-flop behaviour without substituting a combinational
 * table. */
void checkSequential(Component& component)
{
    const auto inputs = component.getStateInPins();
    const bool q = component.getStateOutPin(0), notQ = component.getStateOutPin(1);
    const auto table = buildTruthTable(component, {}, {});
    require(
        table.headers.size() == static_cast<std::size_t>(table.inputCount + 2),
        "Sequential table omitted independent output columns."
    );
    for (int row = 0; row < static_cast<int>(table.rows.size()); ++row)
    {
        auto copy = component.clone();
        for (int i = 0; i < table.inputCount; ++i)
            copy->setStateInPin(i, table.rows[row][i] == "1");
        if (component.clockInputPin() >= 0)
            copy->onClockEdge(true);
        else
            copy->evaluate();
        require(
            copy->getStateOutPin(0) == resolve(table.rows[row][table.inputCount], q, notQ) &&
                copy->getStateOutPin(1) == resolve(table.rows[row][table.inputCount + 1], q, notQ),
            "Sequential next-state table disagrees with runtime behavior."
        );
    }
    require(
        component.getStateInPins() == inputs && component.getStateOutPin(0) == q && component.getStateOutPin(1) == notQ,
        "Table evaluation disturbed stored state."
    );
}


/** @brief Verify latch and flip-flop tables retain their hold, toggle and active-edge semantics. */
void sequential()
{
    for (auto type : {FlipFlopType::D, FlipFlopType::T, FlipFlopType::JK})
        for (bool q : {false, true})
        {
            FlipFlop flip(0, type);
            flip.setStateInPin(0, q);
            flip.onClockEdge(true);
            checkSequential(flip);
            const auto table = buildTruthTable(flip, {}, {});
            require(
                table.inputCount == (type == FlipFlopType::JK ? 2 : 1) &&
                    table.notes.front().find("CLK 0 -> 1") != std::string::npos,
                "Flip-flop table treated the clock as a level input."
            );
        }
    for (auto type : {LatchType::SR_LATCH, LatchType::D_LATCH})
        for (int seed = 0; seed < 4; ++seed)
        {
            Latch latch(0, type);
            latch.setStateInPin(0, (seed >> 1) & 1);
            latch.setStateInPin(1, seed & 1);
            latch.evaluate();
            checkSequential(latch);
        }
}


/** @brief Build a reusable combinational fixture with a directly mapped input/output interface. */
SubcircuitBehavior passThrough(int count = 1)
{
    Circuit circuit;
    const int removed = circuit.addGate(NOT);
    circuit.delComponent(removed);
    SubcircuitBehavior design;
    for (int i = 0; i < count; ++i)
    {
        const int input = circuit.addInputPin();
        const int output = circuit.addOutputPin();
        require(circuit.connectComponents(input, 0, output, 0), "Cannot connect table fixture.");
        design.inputs.push_back(input);
        design.outputs.push_back(output);
    }
    design.circuit = std::make_shared<Circuit>(std::move(circuit));
    return design;
}


bool noteContains(const TruthTable& table, const char* message)
{
    return std::any_of(
        table.notes.begin(), table.notes.end(),
        [&](const auto& note) { return note.find(message) != std::string::npos; }
    );
}


/** @brief Wrap an authored design in another reusable component to exercise nested truth-table evaluation. */
SubcircuitBehavior wrap(const SubcircuitBehavior& inner)
{
    Circuit circuit;
    SubcircuitBehavior outer;
    const int nested = circuit.addComponent(std::make_unique<Subcircuit>(inner));
    for (int i = 0; i < static_cast<int>(inner.inputs.size()); ++i)
    {
        const int input = circuit.addInputPin();
        circuit.connectComponents(input, 0, nested, i);
        outer.inputs.push_back(input);
    }
    for (int i = 0; i < static_cast<int>(inner.outputs.size()); ++i)
    {
        const int output = circuit.addOutputPin();
        circuit.connectComponents(nested, i, output, 0);
        outer.outputs.push_back(output);
    }
    outer.circuit = std::make_shared<Circuit>(std::move(circuit));
    return outer;
}


/** @brief Verify custom combinational tables preserve labels, nesting, all combinations and live runtime state. */
void customCircuits()
{
    Circuit circuit;
    const int a = circuit.addInputPin(), b = circuit.addInputPin();
    const int sum = circuit.addGate(XOR), carry = circuit.addGate(AND);
    static_cast<Gate*>(circuit.getComponent(carry))->setInputInverted(0, true);
    for (int gate : {sum, carry})
    {
        circuit.connectComponents(a, 0, gate, 0);
        circuit.connectComponents(b, 0, gate, 1);
    }
    const int outSum = circuit.addOutputPin(), outCarry = circuit.addOutputPin();
    circuit.connectComponents(sum, 0, outSum, 0);
    circuit.connectComponents(carry, 0, outCarry, 0);
    SubcircuitBehavior design;
    design.inputs = {b, a}; // External order differs from internal component IDs.
    design.outputs = {outCarry, outSum};
    design.circuit = std::make_shared<Circuit>(std::move(circuit));
    const std::vector<PinUI> inputs{
        {PinType::INPUT, 1, PinState::OFF, {}, "", "A"}, {PinType::INPUT, 0, PinState::OFF, {}, "", "B"}
    };
    const std::vector<PinUI> outputs{
        {PinType::OUTPUT, 1, PinState::OFF, {}, "", "Sum"}, {PinType::OUTPUT, 0, PinState::OFF, {}, "", "Masked"}
    };
    for (bool nested : {false, true})
    {
        Subcircuit sub(nested ? wrap(design) : design);
        sub.setStateInPin(0, true);
        sub.setStateInPin(1, false);
        sub.evaluate();
        const auto states = sub.getStateInPins();
        const auto result = sub.simulationResult();
        const auto& runtime = sub.runtimeCircuit();
        const auto ids = runtime.componentIds();
        std::vector<std::vector<bool>> internalInputs, internalOutputs;
        for (int id : ids)
        {
            const auto* component = runtime.getComponent(id);
            internalInputs.push_back(component->getStateInPins());
            std::vector<bool> values;
            for (int pin = 0; pin < component->getOutputPinCount(); ++pin)
                values.push_back(component->getStateOutPin(pin));
            internalOutputs.push_back(values);
        }
        const auto table = buildTruthTable(sub, inputs, outputs);
        require(
            hasTruthTable(&sub) && table.headers == std::vector<std::string>{"B", "A", "Masked", "Sum"},
            "Custom table lost named logical port order."
        );
        require(
            table.rows ==
                std::vector<std::vector<std::string>>{
                    {"0", "0", "0", "0"}, {"0", "1", "0", "1"}, {"1", "0", "1", "1"}, {"1", "1", "0", "0"}
                },
            "Custom/nested table lost input inversion or independent outputs."
        );
        require(
            sub.getStateInPins() == states && sub.getStateOutPin(0) && sub.getStateOutPin(1) &&
                sub.simulationResult() == result,
            "Truth table mutated live custom state."
        );
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            const auto* component = runtime.getComponent(ids[i]);
            require(component->getStateInPins() == internalInputs[i], "Internal live inputs changed.");
            for (int pin = 0; pin < component->getOutputPinCount(); ++pin)
                require(component->getStateOutPin(pin) == internalOutputs[i][pin], "Internal live outputs changed.");
        }
    }
    Subcircuit eight(passThrough(8));
    const auto wide = buildTruthTable(eight, {}, {});
    require(wide.rows.size() == 256 && wide.headers.size() == 16, "Eight-input custom circuit was truncated.");
    for (const auto& row : wide.rows)
        require(
            std::equal(row.begin(), row.begin() + 8, row.begin() + 8), "Custom circuit omitted some output columns."
        );
    Subcircuit nine(passThrough(9));
    const auto tooBig = buildTruthTable(nine, {}, {});
    require(
        tooBig.rows.empty() && noteContains(tooBig, "Truth table too big"), "Nine-input table not rejected clearly."
    );
}


/** @brief Reject stateful, clocked, cyclic or excessive nested designs before presenting unreliable truth-table
 * results. */
void customRestrictions()
{
    for (int kind = 0; kind < 5; ++kind)
    {
        auto design = passThrough();
        auto circuit = std::make_shared<Circuit>(*design.circuit);
        if (kind == 0)
            circuit->addClock();
        else if (kind == 1)
            circuit->addLatch(LatchType::SR_LATCH);
        else if (kind == 2)
            circuit->addComponent(std::make_unique<FlipFlop>(-1, FlipFlopType::D));
        else if (kind == 3)
        {
            const int loop = circuit->addGate(OR);
            circuit->connectComponents(loop, 0, loop, 0); // Can settle, but still contains feedback.
        }
        else
        {
            const int first = circuit->addGate(OR), second = circuit->addGate(AND);
            circuit->connectComponents(first, 0, second, 0);
            circuit->connectComponents(second, 0, first, 0);
        }
        design.circuit = circuit;
        for (bool nested : {false, true})
        {
            Subcircuit sub(nested ? wrap(design) : design);
            const auto table = buildTruthTable(sub, {}, {});
            const char* reason = kind == 0 ? "clocks" : kind < 3 ? "stored state" : "feedback";
            require(
                table.rows.empty() && noteContains(table, reason),
                "Nested time/state/feedback behavior was presented as combinational."
            );
        }
    }
    auto costly = passThrough(8);
    auto circuit = std::make_shared<Circuit>(*costly.circuit);
    for (int i = 0; i < 500; ++i)
        circuit->addGate(AND);
    costly.circuit = circuit;
    Subcircuit sub(costly);
    sub.setStateInPin(0, true);
    sub.evaluate();
    const auto table = buildTruthTable(sub, {}, {});
    require(
        table.rows.empty() && noteContains(table, "Calculation limit reached") && sub.getStateOutPin(0),
        "Total table work was not bounded, partial results leaked or live state changed."
    );
}

} // namespace


int main()
{
    try
    {
        gates();
        sequential();
        customCircuits();
        customRestrictions();
        std::cout << "PASS: educational truth tables\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
