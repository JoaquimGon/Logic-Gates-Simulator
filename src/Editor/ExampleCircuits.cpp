#include "Editor/ExampleCircuits.h"

#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Geometry/WireRouting.h"

#include <array>
#include <stdexcept>
#include <string>

namespace
{
constexpr std::array examples{
    CircuitExampleInfo{CircuitExample::LogicGates, "Logic gates", "Toggle A and B to compare AND, OR and XOR."},
    CircuitExampleInfo{CircuitExample::HalfAdder, "Half adder", "Add A and B: Sum is XOR; Carry is AND."},
    CircuitExampleInfo{CircuitExample::FullAdder, "Full adder", "Add A, B and Carry in to produce Sum and Carry out."},
    CircuitExampleInfo{CircuitExample::Counter, "2-bit counter", "Press Run or Half cycle to count 00, 01, 10, 11."}
};


class Builder
{
  public:
    Scene scene;


    /** @brief Create and label an example component through the same validation path as ordinary edits. */
    int add(const char* definition, GridCoords position, const char* label, bool high = false)
    {
        ComponentOverrides overrides;
        if (definition == std::string_view{BuiltinComponentIds::Input})
            overrides.inputState = high;
        const int id = scene.addComponent(definition, position, overrides);
        const auto result = EditorActions(scene).apply(
            {ConfigureComponentProperties{.componentId = id, .label = label, .labelVisible = true}}
        );
        if (!result)
            throw std::invalid_argument(result.message);
        return id;
    }


    /** @brief Route an example connection between absolute pin anchors and reject unrouteable geometry. */
    void connect(int source, unsigned output, int destination, unsigned input)
    {
        const auto& from = *scene.getCommittedComponentView(source);
        const auto& to = *scene.getCommittedComponentView(destination);
        const auto start = from.getAbsolutePinGridPos(from.getOutputPins().at(output));
        const auto end = to.getAbsolutePinGridPos(to.getInputPins().at(input));
        const auto path = routeWire(
            start, end, scene.committedGeometry(), scene.getWires(),
            scene.netOfPin({source, static_cast<int>(output)}, PinType::OUTPUT)
        );
        if (!path)
            throw std::invalid_argument("Cannot route the example circuit.");
        const auto result = EditorActions(scene).apply({AddWire{*path}});
        if (!result)
            throw std::invalid_argument(result.message);
    }
};
} // namespace


std::span<const CircuitExampleInfo> circuitExamples()
{
    return examples;
}


Scene buildExampleCircuit(CircuitExample example)
{
    Builder b;
    if (example == CircuitExample::LogicGates)
    {
        const int a = b.add(BuiltinComponentIds::Input, {-22, 6}, "A", true);
        const int inputB = b.add(BuiltinComponentIds::Input, {-22, -6}, "B");
        const std::array gates{
            b.add(BuiltinComponentIds::And, {-2, 12}, "AND"), b.add(BuiltinComponentIds::Or, {-2, 0}, "OR"),
            b.add(BuiltinComponentIds::Xor, {-2, -12}, "XOR")
        };
        const std::array outputs{
            b.add(BuiltinComponentIds::Output, {14, 12}, "A AND B"),
            b.add(BuiltinComponentIds::Output, {14, 0}, "A OR B"),
            b.add(BuiltinComponentIds::Output, {14, -12}, "A XOR B")
        };
        for (std::size_t i = 0; i < gates.size(); ++i)
        {
            b.connect(a, 0, gates[i], 0);
            b.connect(inputB, 0, gates[i], 1);
            b.connect(gates[i], 0, outputs[i], 0);
        }
    }
    else if (example == CircuitExample::HalfAdder)
    {
        const int a = b.add(BuiltinComponentIds::Input, {-22, 8}, "A", true);
        const int inputB = b.add(BuiltinComponentIds::Input, {-22, -8}, "B", true);
        const int sum = b.add(BuiltinComponentIds::Xor, {-6, 8}, "XOR");
        const int carry = b.add(BuiltinComponentIds::And, {-6, -8}, "AND");
        const int outSum = b.add(BuiltinComponentIds::Output, {16, 8}, "Sum");
        const int outCarry = b.add(BuiltinComponentIds::Output, {16, -8}, "Carry");
        b.connect(a, 0, sum, 0);
        b.connect(inputB, 0, sum, 1);
        b.connect(a, 0, carry, 0);
        b.connect(inputB, 0, carry, 1);
        b.connect(sum, 0, outSum, 0);
        b.connect(carry, 0, outCarry, 0);
    }
    else if (example == CircuitExample::FullAdder)
    {
        const int a = b.add(BuiltinComponentIds::Input, {-30, 14}, "A", true);
        const int inputB = b.add(BuiltinComponentIds::Input, {-30, 0}, "B", true);
        const int carryIn = b.add(BuiltinComponentIds::Input, {-30, -14}, "Carry in", true);
        const int xorAB = b.add(BuiltinComponentIds::Xor, {-12, 12}, "XOR");
        const int andAB = b.add(BuiltinComponentIds::And, {-12, -10}, "AND");
        const int sum = b.add(BuiltinComponentIds::Xor, {4, 12}, "XOR");
        const int andCarry = b.add(BuiltinComponentIds::And, {4, -2}, "AND");
        const int carry = b.add(BuiltinComponentIds::Or, {18, -10}, "OR");
        const int outSum = b.add(BuiltinComponentIds::Output, {30, 12}, "Sum");
        const int outCarry = b.add(BuiltinComponentIds::Output, {30, -10}, "Carry out");
        b.connect(a, 0, xorAB, 0);
        b.connect(inputB, 0, xorAB, 1);
        b.connect(a, 0, andAB, 0);
        b.connect(inputB, 0, andAB, 1);
        b.connect(xorAB, 0, sum, 0);
        b.connect(carryIn, 0, sum, 1);
        b.connect(xorAB, 0, andCarry, 0);
        b.connect(carryIn, 0, andCarry, 1);
        b.connect(andAB, 0, carry, 0);
        b.connect(andCarry, 0, carry, 1);
        b.connect(sum, 0, outSum, 0);
        b.connect(carry, 0, outCarry, 0);
    }
    else if (example == CircuitExample::Counter)
    {
        const int clock = b.add(BuiltinComponentIds::Clock, {-22, -8}, "1 Hz clock");
        const int low = b.add(BuiltinComponentIds::DFlipFlop, {-8, 8}, "Bit 0");
        const int high = b.add(BuiltinComponentIds::DFlipFlop, {10, 8}, "Bit 1");
        const int nextHigh = b.add(BuiltinComponentIds::Xor, {0, -8}, "XOR");
        const int outLow = b.add(BuiltinComponentIds::Output, {26, 14}, "Bit 0 (1)");
        const int outHigh = b.add(BuiltinComponentIds::Output, {26, 4}, "Bit 1 (2)");
        b.connect(low, 1, low, 0);
        b.connect(clock, 0, low, 1);
        b.connect(clock, 0, high, 1);
        b.connect(low, 0, nextHigh, 0);
        b.connect(high, 0, nextHigh, 1);
        b.connect(nextHigh, 0, high, 0);
        b.connect(low, 0, outLow, 0);
        b.connect(high, 0, outHigh, 0);
        b.scene.togglePauseAllClocks();
    }
    else
        throw std::invalid_argument("Unknown example circuit.");
    if (b.scene.propagate() != SimulationResult::OK || b.scene.getShortedNetCount() != 0)
        throw std::invalid_argument("Example circuit did not settle correctly.");
    b.scene.syncVisuals();
    return std::move(b.scene);
}
