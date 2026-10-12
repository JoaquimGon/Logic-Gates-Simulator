#include "Components/Definitions/NativeDefinitions.h"
#include "Components/FlipFlop.h"
#include "Editor/Actions/EditHistory.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Editor/Subcircuits.h"
#include "Persistence/CircuitFile.h"
#include "Simulation/Subcircuit.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct Fixture
{
    Circuit circuit;
    int data = circuit.addInputPin(), second = circuit.addInputPin(), clk = circuit.addInputPin();
    int memory;

    explicit Fixture(FlipFlopType type)
    {
        memory = circuit.addComponent(std::make_unique<FlipFlop>(-1, type));
        circuit.connectComponents(data, 0, memory, 0);
        if (type == FlipFlopType::JK)
            circuit.connectComponents(second, 0, memory, 1);
        circuit.connectComponents(clk, 0, memory, circuit.getComponent(memory)->clockInputPin());
        circuit.propagate();
    }

    void inputs(bool a, bool b = false)
    {
        static_cast<InputPin*>(circuit.getComponent(data))->setState(a);
        static_cast<InputPin*>(circuit.getComponent(second))->setState(b);
        circuit.propagate();
    }

    void clock(bool high)
    {
        static_cast<InputPin*>(circuit.getComponent(clk))->setState(high);
        circuit.propagate();
    }

    void pulse()
    {
        clock(false);
        clock(true);
    }

    bool q() const { return circuit.getComponent(memory)->getStateOutPin(0); }

    void check(bool expected)
    {
        require(
            q() == expected && circuit.getComponent(memory)->getStateOutPin(1) != expected,
            "Flip-flop Q/~Q disagreed with its next-state table."
        );
    }
};

void tables()
{
    for (auto type : {FlipFlopType::D, FlipFlopType::T, FlipFlopType::JK})
        for (bool previous : {false, true})
            for (bool a : {false, true})
                for (bool b : {false, true})
                {
                    Fixture f(type);
                    f.check(false);
                    if (previous)
                    {
                        f.inputs(true, false);
                        f.pulse();
                    }
                    else
                        f.clock(true);
                    f.check(previous);
                    f.inputs(a, b); // Data changes while CLK stays HIGH must not be captured.
                    f.check(previous);
                    f.clock(false);
                    f.check(previous);
                    f.clock(true);
                    const bool expected = type == FlipFlopType::D   ? a
                                          : type == FlipFlopType::T ? previous != a
                                          : a != b                  ? a
                                          : a                       ? !previous
                                                                    : previous;
                    f.check(expected);
                    for (int repeat = 0; repeat < 3; ++repeat)
                        f.circuit.propagate();
                    f.check(expected);
                    auto copy = f.circuit;
                    f.circuit.resetSources();
                    f.circuit.propagate();
                    f.check(expected); // Source reset is a falling edge, not memory erasure.
                    f.inputs(
                        type == FlipFlopType::T ? true : !expected,
                        type == FlipFlopType::JK && expected
                    );
                    f.clock(true);
                    f.check(!expected);
                    require(
                        copy.getComponent(f.memory)->getStateOutPin() == expected,
                        "Copied retained state shared storage with its source."
                    );
                }
}

void edges()
{
    for (bool reversed : {false, true})
    {
        Circuit circuit;
        const int clk = circuit.addClock(2), data = circuit.addInputPin(true);
        const int first = circuit.addComponent(std::make_unique<FlipFlop>(-1, FlipFlopType::D));
        const int second = circuit.addComponent(std::make_unique<FlipFlop>(-1, FlipFlopType::D));
        const int a = reversed ? second : first, b = reversed ? first : second;
        const int q = circuit.addOutputPin(), notQ = circuit.addOutputPin();
        circuit.connectComponents(data, 0, a, 0);
        circuit.connectComponents(a, 0, b, 0);
        circuit.connectComponents(clk, 0, a, 1);
        circuit.connectComponents(clk, 0, b, 1);
        circuit.connectComponents(a, 0, q, 0);
        circuit.connectComponents(a, 1, notQ, 0);
        circuit.stepClocks(1);
        require(
            circuit.getComponent(a)->getStateOutPin() &&
                !circuit.getComponent(b)->getStateOutPin() &&
                circuit.getComponent(q)->getStateInPin(0) &&
                !circuit.getComponent(notQ)->getStateInPin(0),
            "Shared-clock registers advanced two stages or lost independent outputs."
        );
        circuit.stepClocks(2);
        require(
            circuit.getComponent(b)->getStateOutPin(), "Next rising edge missed the second stage."
        );
    }
    Circuit wiring;
    const int high = wiring.addInputPin(true);
    const int memory = wiring.addComponent(std::make_unique<FlipFlop>(-1, FlipFlopType::T));
    wiring.connectComponents(high, 0, memory, 0);
    wiring.connectComponents(high, 0, memory, 1);
    wiring.propagate();
    require(
        !wiring.getComponent(memory)->getStateOutPin(), "Connecting HIGH invented a clock edge."
    );
    auto* source = static_cast<InputPin*>(wiring.getComponent(high));
    source->setState(false);
    wiring.propagate();
    source->setState(true);
    wiring.propagate();
    require(
        wiring.getComponent(memory)->getStateOutPin(), "Manual clock transition missed its edge."
    );
}

void integration()
{
    for (auto type : {FlipFlopType::D, FlipFlopType::T, FlipFlopType::JK})
    {
        Scene scene;
        const int id = scene.addComponent(builtinDefinitionId(type), {0, 0});
        auto* logic = dynamic_cast<FlipFlop*>(scene.getLogicComponent(id));
        const auto* view = scene.getCommittedComponentView(id);
        require(
            logic && logic->getInputPinCount() == int(view->getInputPins().size()) &&
                view->getInputPins().back().label == "CLK" &&
                view->getOutputPins()[1].label == "~Q",
            "Native declaration/factory lost the flip-flop interface."
        );
        EditHistory history;
        history.record(EditorActions(scene).apply(
            {ConfigureComponentProperties{.componentId = id, .orientation = Orientation::Up}}
        ));
        require(
            scene.getCommittedComponentView(id)->getOrientation() == Orientation::Up &&
                history.undo(scene) && history.redo(scene),
            "Flip-flop orientation/history failed."
        );
        auto custom = *scene.getComponentCatalog().find(builtinDefinitionId(type));
        custom.identity = {"test.flip-box", 1};
        require(
            bool(EditorActions(scene).apply(
                {RegisterComponentDefinition{custom}, CreateComponent{custom.identity.id, {20, 0}}}
            )),
            "Native-behavior box registration failed."
        );
        auto loaded = circuitFromJson(circuitToJson(scene, "Flip-flops"));
        require(
            dynamic_cast<FlipFlop*>(loaded.scene.getLogicComponent(0))->getType() == type &&
                dynamic_cast<FlipFlop*>(loaded.scene.getLogicComponent(1))->getType() == type,
            "JSON changed flip-flop/native-box behavior or pin counts."
        );
    }
    Scene authored;
    const int data = authored.addComponent(BuiltinComponentIds::Input, {-20, 0});
    authored.addComponent(BuiltinComponentIds::Clock, {-20, -10});
    const int memory = authored.addComponent(BuiltinComponentIds::DFlipFlop, {0, 0});
    authored.addComponent(BuiltinComponentIds::Output, {20, 0});
    const auto wired = EditorActions(authored).apply(
        {AddWire{{{-19, 0}, {-19, 1}, {-3, 1}}},
         AddWire{{{-19, -10}, {-19, -1}, {-3, -1}}},
         AddWire{{{3, 1}, {19, 1}, {19, 0}}}}
    );
    require(bool(wired), "Authored flip-flop wiring was rejected.");
    authored.handleClick(data);
    authored.stepAllClocks();
    require(
        authored.getLogicComponent(memory)->getStateOutPin(), "Authored clock missed D capture."
    );
    auto loaded = circuitFromJson(circuitToJson(authored, "Register"));
    require(
        !loaded.scene.getLogicComponent(memory)->getStateOutPin(),
        "File load retained runtime memory."
    );
    authored.setInterfaceNamingRequired(true);
    auto design = makeSubcircuit(authored, {"subcircuit.register", 1}, "Register");
    const auto& behavior = std::get<SubcircuitBehavior>(design.behavior);
    Circuit parent;
    const int high = parent.addInputPin(true);
    const int a = parent.addComponent(std::make_unique<Subcircuit>(behavior));
    const int b = parent.addComponent(std::make_unique<Subcircuit>(behavior));
    parent.connectComponents(high, 0, a, 0);
    parent.stepClocks();
    require(
        parent.getComponent(a)->getStateOutPin() && !parent.getComponent(b)->getStateOutPin(),
        "Nested flip-flops lost clock edges or shared retained state across instances."
    );
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string group = argc > 1 ? argv[1] : "";
        if (group == "flip_flop_tables")
            tables();
        else if (group == "flip_flop_edges")
            edges();
        else if (group == "flip_flop_integration")
            integration();
        else
            throw std::runtime_error("Unknown flip-flop test.");
        std::cout << "PASS: " << group << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
