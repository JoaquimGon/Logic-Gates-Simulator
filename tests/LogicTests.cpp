#include "Core/Scene.h"
#include "Logic/Circuit.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Exception, typename Action>
void expectThrows(Action action)
{
    try
    {
        action();
    }
    catch (const Exception&)
    {
        return;
    }
    throw std::runtime_error("Expected exception was not thrown.");
}

void gateTruthTables()
{
    for (GateType type : {AND, NAND, OR, NOR, XOR, NXOR})
    {
        for (int count : {2, 3, 4, 6})
        {
            Gate gate(0, type, count);
            require(gate.getInputPinCount() == count, "Gate arity was not preserved.");
            require(gate.getOutputPinCount() == 1, "Gate must have one output.");
            for (int mask = 0; mask < (1 << count); ++mask)
            {
                int highCount = 0;
                for (int pin = 0; pin < count; ++pin)
                {
                    bool high = (mask & (1 << pin)) != 0;
                    gate.setStateInPin(pin, high);
                    highCount += high;
                }

                bool expected = false;
                switch (type)
                {
                case AND:
                    expected = highCount == count;
                    break;
                case NAND:
                    expected = highCount != count;
                    break;
                case OR:
                    expected = highCount > 0;
                    break;
                case NOR:
                    expected = highCount == 0;
                    break;
                case XOR:
                    expected = highCount % 2 == 1;
                    break;
                case NXOR:
                    expected = highCount % 2 == 0;
                    break;
                default:
                    break;
                }
                gate.evaluate();
                require(gate.getStateOutPin() == expected, "Multi-input truth table mismatch.");
            }
        }
    }

    Gate inverter(0, NOT);
    for (bool input : {false, true})
    {
        inverter.setStateInPin(0, input);
        inverter.evaluate();
        require(inverter.getStateOutPin() == !input, "NOT truth table mismatch.");
    }
    require(Gate(0, AND).getInputPinCount() == 2, "Default gate arity changed.");
    expectThrows<std::invalid_argument>([] { Gate gate(0, AND, 0); });
    expectThrows<std::invalid_argument>([] { Gate gate(0, OR, 1); });
    expectThrows<std::invalid_argument>([] { Gate gate(0, NOT, 2); });
    expectThrows<std::invalid_argument>([] { Gate gate(0, XOR, -1); });
    expectThrows<std::invalid_argument>([] { Gate gate(0, static_cast<GateType>(999), 2); });
}

class PinProbe : public Component
{
  public:
    using Component::Component;

    void evaluate() override
    {
        for (int pin = 0; pin < getOutputPinCount(); ++pin)
            setStateOutPin(pin, getStateInPin(pin));
    }
};

void componentPinBounds()
{
    PinProbe probe(0, 4, 4);
    for (int pin = 0; pin < 4; ++pin)
        probe.setStateInPin(pin, pin % 2 != 0);
    probe.evaluate();
    for (int pin = 0; pin < 4; ++pin)
        require(probe.getStateOutPin(pin) == (pin % 2 != 0), "Generic pin storage mismatch.");

    expectThrows<std::out_of_range>([&] { probe.setStateInPin(-1, true); });
    expectThrows<std::out_of_range>([&] { probe.setStateInPin(4, true); });
    expectThrows<std::out_of_range>([&] { probe.getStateInPin(-1); });
    expectThrows<std::out_of_range>([&] { probe.getStateOutPin(4); });
    expectThrows<std::invalid_argument>([] { PinProbe probe(0, -1, 0); });
    expectThrows<std::invalid_argument>([] { PinProbe probe(0, 0, -1); });

    InputPin source(1);
    expectThrows<std::out_of_range>([&] { source.setStateInPin(0, true); });
    expectThrows<std::out_of_range>([&] { source.getStateOutPin(1); });
    Latch latch(2, LatchType::SR_LATCH);
    expectThrows<std::out_of_range>([&] { latch.getStateOutPin(-1); });
    expectThrows<std::out_of_range>([&] { latch.getStateOutPin(2); });
}

void connectionEndpoints()
{
    Circuit circuit;
    int latch = circuit.addLatch(LatchType::SR_LATCH);
    int gate = circuit.addGate(OR, 4);

    require(circuit.connectComponents(latch, 0, gate, 2), "Q connection failed.");
    require(circuit.connectComponents(latch, 1, gate, 3), "~Q connection failed.");
    require(circuit.connectComponents(latch, 1, gate, 3), "Identical edge must be idempotent.");
    require(!circuit.connectComponents(latch, 0, gate, 3), "Different output replaced a driver.");
    require(circuit.getComponent(latch)->getOutConnections().size() == 2, "Duplicate edge added.");
    require(!circuit.connectComponents(latch, -1, gate, 0), "Negative output accepted.");
    require(!circuit.connectComponents(latch, 2, gate, 0), "Missing output accepted.");
    require(!circuit.connectComponents(latch, 0, gate, -1), "Negative input accepted.");
    require(!circuit.connectComponents(latch, 0, gate, 4), "Missing input accepted.");
    require(!circuit.connectComponents(-1, 0, gate, 0), "Missing component accepted.");
    require(!circuit.connectComponents(gate, 0, gate, 0), "Self-edge accepted.");
    require(!circuit.connectComponents(gate, 0, latch, 0), "Cycle accepted.");

    require(circuit.propagate() == EvalOrderResult::OK, "Propagation failed.");
    require(!circuit.getComponent(gate)->getStateInPin(2), "Q did not reach the chosen input.");
    require(circuit.getComponent(gate)->getStateInPin(3), "~Q did not reach the chosen input.");

    circuit.disconnectComponents(latch, 0, gate, 3);
    require(circuit.getComponent(gate)->getStateInPin(3), "Mismatched disconnect reset an input.");
    circuit.disconnectComponents(latch, 1, gate, 3);
    require(!circuit.getComponent(gate)->getStateInPin(3), "Disconnect did not reset its input.");
    require(circuit.getComponent(gate)->getInConnections().size() == 1, "Wrong edge removed.");
    require(circuit.getComponent(latch)->getOutConnections().size() == 1, "Edge lists diverged.");
    require(circuit.connectComponents(latch, 1, gate, 3), "Reconnect failed.");
    require(circuit.propagate() == EvalOrderResult::OK, "Reconnect propagation failed.");
    require(circuit.getComponent(gate)->getStateInPin(3), "Reconnect lost output index.");
}

void latchOutputPropagation()
{
    for (LatchType type : {LatchType::SR_LATCH, LatchType::D_LATCH})
    {
        Circuit circuit;
        int first = circuit.addInputPin();
        int second = circuit.addInputPin();
        int latch = circuit.addLatch(type);
        int qSink = circuit.addGate(NOT);
        int notQSink = circuit.addGate(NOT);
        int fanout = circuit.addGate(AND, 3);
        require(circuit.connectComponents(first, 0, latch, 0), "First latch input failed.");
        require(circuit.connectComponents(second, 0, latch, 1), "Second latch input failed.");
        require(circuit.connectComponents(latch, 0, qSink, 0), "Q sink failed.");
        require(circuit.connectComponents(latch, 1, notQSink, 0), "~Q sink failed.");
        for (int pin = 0; pin < 3; ++pin)
            require(circuit.connectComponents(latch, 1, fanout, pin), "Fanout connection failed.");

        auto verify = [&](bool firstState, bool secondState, bool q, bool notQ)
        {
            static_cast<InputPin*>(circuit.getComponent(first))->setState(firstState);
            static_cast<InputPin*>(circuit.getComponent(second))->setState(secondState);
            circuit.markStateDirty();
            require(circuit.propagate() == EvalOrderResult::OK, "Latch propagation failed.");
            require(circuit.getComponent(latch)->getStateOutPin(0) == q, "Q state mismatch.");
            require(circuit.getComponent(latch)->getStateOutPin(1) == notQ, "~Q state mismatch.");
            require(circuit.getComponent(qSink)->getStateOutPin() == !q, "Q sink state mismatch.");
            require(
                circuit.getComponent(notQSink)->getStateOutPin() == !notQ, "~Q sink state mismatch."
            );
            for (int pin = 0; pin < 3; ++pin)
                require(
                    circuit.getComponent(fanout)->getStateInPin(pin) == notQ,
                    "Fanout lost the source output index."
                );
        };

        verify(false, false, false, true);
        if (type == LatchType::SR_LATCH)
        {
            verify(true, false, true, false);
            verify(false, false, true, false);
            verify(false, true, false, true);
            verify(true, true, false, false);
        }
        else
        {
            verify(true, true, true, false);
            verify(false, false, true, false);
            verify(false, true, false, true);
        }
    }
}

void connectionLifecycle()
{
    Circuit circuit;
    int latch = circuit.addLatch(LatchType::SR_LATCH);
    int first = circuit.addGate(OR, 3);
    int second = circuit.addGate(NOT);
    require(circuit.connectComponents(latch, 0, first, 0), "First edge failed.");
    require(circuit.connectComponents(latch, 1, first, 2), "Second edge failed.");
    require(circuit.connectComponents(latch, 1, second, 0), "Third edge failed.");
    circuit.propagate();

    circuit.delComponent(first);
    require(
        circuit.getComponent(latch)->getOutConnections().size() == 1,
        "Deleting a sink removed unrelated edges."
    );
    require(circuit.getComponent(second)->getStateInPin(0), "Unrelated sink changed.");
    circuit.delComponent(latch);
    require(
        circuit.getComponent(second)->getInConnections().empty(), "Deleted driver left an edge."
    );
    require(!circuit.getComponent(second)->getStateInPin(0), "Deleted driver left a signal.");
    require(circuit.propagate() == EvalOrderResult::OK, "Deletion left invalid evaluation order.");

    int source = circuit.addInputPin(true);
    int gate = circuit.addGate(AND, 4);
    for (int pin = 0; pin < 4; ++pin)
        require(circuit.connectComponents(source, 0, gate, pin), "Input fanout failed.");
    circuit.propagate();
    require(circuit.getComponent(gate)->getStateOutPin(), "Four-input AND failed.");
    circuit.clearConnections();
    require(circuit.getComponent(source)->getOutConnections().empty(), "Clear left source edges.");
    require(circuit.getComponent(gate)->getInConnections().empty(), "Clear left sink edges.");
    for (int pin = 0; pin < 4; ++pin)
        require(!circuit.getComponent(gate)->getStateInPin(pin), "Clear missed an input.");
}

Wire route(GridCoords from, GridCoords to)
{
    Wire wire;
    wire.setPath({from, to});
    return wire;
}

void scenePinLayouts()
{
    Scene scene;
    int latch = scene.addLatch(LatchType::SR_LATCH, {0, 0});
    std::vector<PinUI> notInput{{PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 0}}};
    std::vector<PinUI> notOutput{{PinType::OUTPUT, 0, PinState::DISCONNECTED, {1, 0}}};
    int qSink = scene.addGate(NOT, {10, 1}, {0.2f, 0.1f}, "NOTgate", notInput, notOutput);
    int notQSink = scene.addGate(NOT, {10, -1}, {0.2f, 0.1f}, "NOTgate", notInput, notOutput);
    scene.commitWire(route({3, 1}, {8, 1}));
    scene.commitWire(route({3, -1}, {8, -1}));
    require(scene.propagate() == EvalOrderResult::OK, "Scene propagation failed.");
    scene.syncVisuals();
    require(scene.getLogicComponent(qSink)->getStateOutPin(), "Scene Q wiring mismatch.");
    require(!scene.getLogicComponent(notQSink)->getStateOutPin(), "Scene ~Q wiring mismatch.");

    auto& latchOutputs = scene.getComponentView(latch)->getOutputPins();
    std::reverse(latchOutputs.begin(), latchOutputs.end());
    scene.rebuildNets();
    scene.propagate();
    scene.syncVisuals();
    require(latchOutputs[0].state == PinState::ON, "Reordered ~Q visual read Q.");
    require(latchOutputs[1].state == PinState::OFF, "Reordered Q visual read ~Q.");
    auto* latchView = static_cast<LatchView*>(scene.getComponentView(latch));
    require(
        latchView->getOutputLabel(latchOutputs[0].pin_index) == "~Q",
        "Reordered output label lost pin identity."
    );
    require(
        latchView->getOutputLabel(latchOutputs[1].pin_index) == "Q", "Q label lost pin identity."
    );
    auto& latchInputs = latchView->getInputPins();
    std::reverse(latchInputs.begin(), latchInputs.end());
    require(
        latchView->getInputLabel(latchInputs[0].pin_index) == "R",
        "Reordered input label lost pin identity."
    );
    for (const auto& [id, net] : scene.getNets())
    {
        const auto driver = net.getDriver();
        if (driver && driver->componentId == latch)
        {
            bool expected = driver->pinIndex == 1;
            require((net.getState() == PinState::ON) == expected, "Net and simulation disagree.");
            for (WireId wireId : net.getGeometry())
                require(
                    scene.getWire(wireId)->getState() == net.getState(), "Wire and net disagree."
                );
        }
    }

    Scene multi;
    std::vector<PinUI> inputs{
        {PinType::INPUT, 2, PinState::DISCONNECTED, {-2, -2}},
        {PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 2}},
        {PinType::INPUT, 1, PinState::DISCONNECTED, {-2, 0}}
    };
    std::vector<PinUI> outputs{{PinType::OUTPUT, 0, PinState::DISCONNECTED, {2, 0}}};
    int gate = multi.addGate(AND, {0, 0}, {0.2f, 0.3f}, "ANDgate", inputs, outputs);
    require(
        multi.getLogicComponent(gate)->getInputPinCount() == 3,
        "Visual input count was not used for logic."
    );
    std::vector<int> sources;
    for (int pin = 0; pin < 3; ++pin)
    {
        int y = 2 - 2 * pin;
        sources.push_back(multi.addInputPin({-10, y}, {0.15f, 0.15f}, "inputPin", true));
        multi.commitWire(route({-9, y}, {-2, y}));
    }
    multi.propagate();
    multi.syncVisuals();
    require(multi.getLogicComponent(gate)->getStateOutPin(), "Scene three-input AND failed.");

    static_cast<InputPin*>(multi.getLogicComponent(sources[2]))->setState(false);
    multi.markSimulationDirty();
    multi.propagate();
    multi.syncVisuals();
    require(!multi.getLogicComponent(gate)->getStateOutPin(), "Third input was ignored.");
    const auto& visibleInputs = multi.getComponentView(gate)->getInputPins();
    require(visibleInputs[0].state == PinState::OFF, "Reordered input visual read wrong pin.");
    require(visibleInputs[1].state == PinState::ON, "High input visual read wrong pin.");

    size_t before = multi.getComponentCount();
    auto badInputs = inputs;
    badInputs[0].pin_index = 0;
    expectThrows<std::invalid_argument>(
        [&] { multi.addGate(AND, {20, 0}, {0.2f, 0.3f}, "ANDgate", badInputs, outputs); }
    );
    badInputs = inputs;
    badInputs[0].pin_index = 3;
    expectThrows<std::invalid_argument>(
        [&] { multi.addGate(AND, {20, 0}, {0.2f, 0.3f}, "ANDgate", badInputs, outputs); }
    );
    badInputs = inputs;
    badInputs[0].type = PinType::OUTPUT;
    expectThrows<std::invalid_argument>(
        [&] { multi.addGate(AND, {20, 0}, {0.2f, 0.3f}, "ANDgate", badInputs, outputs); }
    );
    expectThrows<std::invalid_argument>(
        [&] { multi.addGate(NOT, {20, 0}, {0.2f, 0.3f}, "NOTgate", inputs, outputs); }
    );
    expectThrows<std::invalid_argument>(
        [&] { multi.addGate(AND, {20, 0}, {0.2f, 0.3f}, "ANDgate", inputs, {}); }
    );
    require(multi.getComponentCount() == before, "Invalid layout created a component.");

    multi.getComponentView(gate)->getInputPins().pop_back();
    expectThrows<std::invalid_argument>([&] { multi.rebuildNets(); });
}

void sourceComponents()
{
    InputPin input(0, true);
    require(
        input.getInputPinCount() == 0 && input.getOutputPinCount() == 1,
        "Input source shape changed."
    );
    require(input.getState(), "Initial input state lost.");
    input.toggle();
    input.evaluate();
    require(!input.getStateOutPin(), "Input toggle failed.");

    Circuit circuit;
    int clockId = circuit.addClock(1.0f);
    int sinkId = circuit.addGate(NOT);
    auto* clock = static_cast<Clock*>(circuit.getComponent(clockId));
    require(circuit.connectComponents(clockId, 0, sinkId, 0), "Clock connection failed.");
    require(circuit.updateClocks(0.5f), "Clock edge missing.");
    circuit.propagate();
    require(!circuit.getComponent(sinkId)->getStateOutPin(), "Clock signal was not propagated.");
    clock->setPaused(true);
    require(!circuit.updateClocks(0.5f), "Paused clock advanced.");
    clock->step();
    circuit.markStateDirty();
    circuit.propagate();
    require(circuit.getComponent(sinkId)->getStateOutPin(), "Manual clock edge was lost.");
}
} // namespace

int main(int argc, char** argv)
{
    const std::pair<const char*, void (*)()> tests[] = {
        {"gate_truth_tables", gateTruthTables},
        {"component_pin_bounds", componentPinBounds},
        {"connection_endpoints", connectionEndpoints},
        {"latch_output_propagation", latchOutputPropagation},
        {"connection_lifecycle", connectionLifecycle},
        {"scene_pin_layouts", scenePinLayouts},
        {"source_components", sourceComponents}
    };

    try
    {
        bool matched = false;
        for (const auto& [name, run] : tests)
        {
            if (argc == 1 || std::string(argv[1]) == name)
            {
                run();
                matched = true;
                std::cout << "PASS: " << name << '\n';
            }
        }
        require(matched, "Unknown test name.");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
