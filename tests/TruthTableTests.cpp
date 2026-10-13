#include "Components/FlipFlop.h"
#include "Components/Gate.h"
#include "Components/InputPin.h"
#include "Components/Latch.h"
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
                require(
                    table.rows.size() == (std::size_t{1} << count),
                    "Gate table omitted combinations."
                );
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
                    gate.getStateInPins() == originalInputs &&
                        gate.getStateOutPin() == originalOutput,
                    "Building a table changed live component state."
                );
            }
    Gate gate(0, AND, 2);
    gate.setInputInverted(0, true);
    const std::vector<PinUI> pins{
        {PinType::INPUT, 1, PinState::OFF, {}, "", "Second"},
        {PinType::INPUT, 0, PinState::ON, {}, "", "First"}
    };
    const auto named =
        buildTruthTable(gate, pins, {{PinType::OUTPUT, 0, PinState::OFF, {}, "", "Result"}});
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
        component.getStateInPins() == inputs && component.getStateOutPin(0) == q &&
            component.getStateOutPin(1) == notQ,
        "Table evaluation disturbed stored state."
    );
}

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
} // namespace

int main()
{
    try
    {
        gates();
        sequential();
        std::cout << "PASS: educational truth tables\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
