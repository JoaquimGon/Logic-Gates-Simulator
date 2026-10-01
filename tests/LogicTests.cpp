#include "Components/Views/LatchView.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Geometry/GeometryQueries.h"
#include "Geometry/WireNormalization.h"
#include "Simulation/Circuit.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

    std::unique_ptr<Component> clone() const override { return std::make_unique<PinProbe>(*this); }

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

    require(
        circuit.tryConnectComponents(-1, 0, gate, 0) == ConnectionResult::INVALID_COMPONENT,
        "Missing component rejection lost its reason."
    );
    require(
        circuit.tryConnectComponents(latch, 2, gate, 0) == ConnectionResult::INVALID_PIN,
        "Invalid output rejection lost its reason."
    );
    require(
        circuit.tryConnectComponents(latch, 0, gate, 4) == ConnectionResult::INVALID_PIN,
        "Invalid input rejection lost its reason."
    );
    require(
        circuit.tryConnectComponents(gate, 0, gate, 0) == ConnectionResult::CYCLE_DETECTED,
        "Self-loop rejection lost its reason."
    );
    require(
        circuit.tryConnectComponents(latch, 0, gate, 2) == ConnectionResult::OK,
        "Valid connection reported a rejection."
    );
    require(circuit.connectComponents(latch, 0, gate, 2), "Q connection failed.");
    require(circuit.connectComponents(latch, 1, gate, 3), "~Q connection failed.");
    require(circuit.connectComponents(latch, 1, gate, 3), "Identical edge must be idempotent.");
    require(!circuit.connectComponents(latch, 0, gate, 3), "Different output replaced a driver.");
    require(
        circuit.tryConnectComponents(latch, 0, gate, 3) == ConnectionResult::INPUT_ALREADY_DRIVEN,
        "Occupied input rejection lost its reason."
    );
    require(
        circuit.tryConnectComponents(gate, 0, latch, 0) == ConnectionResult::CYCLE_DETECTED,
        "Indirect cycle rejection lost its reason."
    );
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

void sceneWireOverlaps()
{
    using Path = std::vector<GridCoords>;
    using Segment = std::tuple<int, int, int, int>;
    const std::pair<Path, Path> cases[] = {
        {{{0, 0}, {6, 0}}, {{2, 0}, {8, 0}}},
        {{{0, 0}, {8, 0}}, {{2, 0}, {6, 0}}},
        {{{0, 0}, {6, 0}}, {{0, 0}, {6, 0}}},
        {{{0, 0}, {0, 6}}, {{0, 2}, {0, 8}}},
        {{{0, 0}, {6, 0}, {6, 4}}, {{2, 0}, {8, 0}, {8, -4}}},
        {{{0, -4}, {0, 0}, {6, 0}}, {{8, 4}, {8, 0}, {2, 0}}},
        {{{0, 0}, {2, 0}, {2, 4}, {8, 4}, {8, 0}, {10, 0}},
         {{0, 0}, {2, 0}, {2, -4}, {8, -4}, {8, 0}, {10, 0}}}
    };

    auto collectSegments = [](const Path& path, std::map<Segment, int>& segments)
    {
        for (size_t i = 1; i < path.size(); ++i)
        {
            GridCoords point = path[i - 1];
            const GridCoords end = path[i];
            const int dx = (end.x > point.x) - (end.x < point.x);
            const int dy = (end.y > point.y) - (end.y < point.y);
            require(dx == 0 || dy == 0, "Overlap merge produced a diagonal segment.");
            while (!(point == end))
            {
                GridCoords next{point.x + dx, point.y + dy};
                segments[{
                    std::min(point.x, next.x),
                    std::min(point.y, next.y),
                    std::max(point.x, next.x),
                    std::max(point.y, next.y)
                }]++;
                point = next;
            }
        }
    };

    for (const auto& [firstPath, secondPath] : cases)
    {
        for (int variant = 0; variant < 8; ++variant)
        {
            Path first = firstPath;
            Path second = secondPath;
            if (variant & 1)
                std::reverse(first.begin(), first.end());
            if (variant & 2)
                std::reverse(second.begin(), second.end());
            if (variant & 4)
                std::swap(first, second);

            std::map<Segment, int> expected;
            collectSegments(first, expected);
            collectSegments(second, expected);
            for (auto& [segment, count] : expected)
                count = 1;

            Scene scene;
            Wire firstWire, secondWire;
            firstWire.setPath(first);
            secondWire.setPath(second);
            scene.commitWire(std::move(firstWire));
            scene.commitWire(std::move(secondWire));

            std::map<Segment, int> actual;
            for (const auto& [id, wire] : scene.getWires())
                collectSegments(wire.getPath(), actual);
            require(actual == expected, "Overlap merge lost geometry or left duplicate segments.");
            require(scene.netCount() == 1, "Overlap merge disconnected the wire network.");

            const auto settledIds = scene.getWireIds();
            scene.rebuildNets();
            require(scene.getWireIds() == settledIds, "Settled overlap geometry was not stable.");
        }
    }
}

void distinctRoutePins()
{
    Scene scene;
    scene.addInputPin({-2, 0}, {0.1f, 0.1f}, "inputPin", true);
    const int upper = scene.addGate(
        NOT,
        {3, 6},
        {0.1f, 0.1f},
        "NOTgate",
        {{PinType::INPUT, 0, PinState::DISCONNECTED, {0, -2}}},
        {{PinType::OUTPUT, 0, PinState::DISCONNECTED, {0, 1}}}
    );
    const int lower = scene.addGate(
        NOT,
        {3, -6},
        {0.1f, 0.1f},
        "NOTgate",
        {{PinType::INPUT, 0, PinState::DISCONNECTED, {0, 2}}},
        {{PinType::OUTPUT, 0, PinState::DISCONNECTED, {0, -1}}}
    );
    Wire top, bottom;
    top.setPath({{0, 0}, {2, 0}, {2, 4}, {8, 4}, {8, 0}, {10, 0}});
    bottom.setPath({{0, 0}, {2, 0}, {2, -4}, {8, -4}, {8, 0}, {10, 0}});
    scene.addWires(top, bottom);
    scene.commitWire(route({-1, 0}, {0, 0}));
    require(
        scene.propagate() == EvalOrderResult::OK &&
            scene.getLogicComponent(upper)->getStateInPin(0) &&
            scene.getLogicComponent(lower)->getStateInPin(0) &&
            scene.netOfPin({upper, 0}, PinType::INPUT) ==
                scene.netOfPin({lower, 0}, PinType::INPUT),
        "Normalization disconnected a pin on one of the distinct bent routes."
    );
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

    const auto* originalLatch = scene.getComponentView(latch);
    ComponentLayout latchLayout{
        originalLatch->getSize(),
        originalLatch->getShaderName(),
        originalLatch->getInputPins(),
        originalLatch->getOutputPins()
    };
    std::reverse(latchLayout.outputs.begin(), latchLayout.outputs.end());
    std::reverse(latchLayout.inputs.begin(), latchLayout.inputs.end());
    require(
        static_cast<bool>(EditorActions(scene).apply({ConfigureComponent{latch, latchLayout}})),
        "Reordered latch layout configuration failed."
    );
    scene.propagate();
    scene.syncVisuals();
    const auto& latchOutputs = scene.getComponentView(latch)->getOutputPins();
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
    const auto& latchInputs = latchView->getInputPins();
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

    auto invalidLayout = GateLayout{{0.2f, 0.3f}, "ANDgate", inputs, outputs};
    invalidLayout.inputs.pop_back(); // Leaves noncontiguous indices: 2 and 0.
    const auto rejected = EditorActions(multi).apply({ConfigureComponent{gate, invalidLayout}});
    require(
        rejected.error == EditError::InvalidConfiguration, "Invalid configuration was accepted."
    );
    require(
        multi.getLogicComponent(gate)->getInputPinCount() == 3,
        "Rejected configuration changed logical arity."
    );
}

void sceneConnectionRejections()
{
    for (bool selfFeedback : {false, true})
    {
        Scene scene;
        auto inverter = [&](GridCoords position)
        {
            return scene.addGate(
                NOT,
                position,
                {0.2f, 0.1f},
                "NOTgate",
                {{PinType::INPUT, 0, PinState::DISCONNECTED, {-2, 0}}},
                {{PinType::OUTPUT, 0, PinState::DISCONNECTED, {1, 0}}}
            );
        };
        int first = inverter({0, 0});
        int second = inverter({10, 0});
        int fanout = inverter({10, -4});
        int source = scene.addInputPin({30, 10}, {0.15f, 0.15f}, "inputPin", true);
        int unrelated = inverter({40, 10});
        int clock = scene.addClock({30, -10}, {0.15f, 0.15f}, "clock");
        int latch = scene.addLatch(LatchType::SR_LATCH, {40, -10});
        scene.commitWire(route({1, 0}, {8, 0}));
        scene.commitWire(route({31, 10}, {38, 10}));
        Wire branch;
        branch.setPath({{1, 0}, {1, -4}, {8, -4}});
        scene.commitWire(std::move(branch));
        require(scene.propagate() == EvalOrderResult::OK, "Initial acyclic scene failed.");
        const bool secondState = scene.getLogicComponent(second)->getStateOutPin();
        const bool latchState = scene.getLogicComponent(latch)->getStateOutPin(1);

        Wire feedback;
        feedback.setPath(
            {{selfFeedback ? 1 : 11, 0}, {selfFeedback ? 1 : 11, 4}, {-2, 4}, {-2, 0}}
        );
        auto feedbackId = scene.commitWire(feedback);
        require(feedbackId && scene.getWire(*feedbackId), "Feedback geometry was lost.");
        require(
            scene.getLastEvalResult() == EvalOrderResult::CYCLE_DETECTED,
            "Rejected feedback was not reported immediately after editing."
        );
        require(scene.isSimulationDirty(), "New rejection did not request a status update.");
        require(
            scene.getEvalOrderSize() == 0, "Blocked scene advertised a valid evaluation order."
        );
        const auto& rejections = scene.getRejectedConnections();
        require(rejections.size() == 1, "Rejected feedback was not diagnosed exactly once.");
        const auto& rejection = rejections.front();
        require(
            rejection.reason == ConnectionResult::CYCLE_DETECTED,
            "Scene rejection lost its cycle reason."
        );
        const Net* rejectedNet = scene.getNet(rejection.netId);
        require(rejectedNet && rejectedNet->hasDriver(), "Rejection referenced a missing net.");
        require(
            *rejectedNet->getDriver() ==
                    PinRef{rejection.connection.srcComponentId, rejection.connection.srcPinIndex} &&
                rejectedNet->isSink(
                    {rejection.connection.destComponentId, rejection.connection.destPinIndex}
                ),
            "Rejection did not retain the rejected endpoints."
        );
        require(
            (rejection.connection.srcComponentId == rejection.connection.destComponentId) ==
                selfFeedback,
            "Self-loop and indirect feedback diagnoses were confused."
        );
        require(
            scene.propagate() == EvalOrderResult::CYCLE_DETECTED,
            "Scene silently simulated a graph with the feedback edge omitted."
        );
        require(
            scene.getLastEvalResult() == EvalOrderResult::CYCLE_DETECTED,
            "Scene status hid the rejected feedback connection."
        );
        require(!scene.isSimulationDirty(), "Blocked scene kept requesting propagation.");
        require(!scene.updateClocks(0.5f), "Blocked scene advanced automatic clocks.");
        require(!scene.getLogicComponent(clock)->getStateOutPin(), "Paused clock changed state.");
        for (const auto& [id, view] : scene.getComponentViewMap())
        {
            require(
                scene.getLogicComponent(id)->getInConnections().empty() &&
                    scene.getLogicComponent(id)->getOutConnections().empty(),
                "Rejected topology left a partial simulation graph."
            );
        }
        require(
            scene.getLogicComponent(second)->getStateOutPin() == secondState &&
                scene.getLogicComponent(latch)->getStateOutPin(1) == latchState,
            "Blocked propagation evaluated components or changed retained state."
        );
        scene.syncVisuals();
        for (const auto& [id, view] : scene.getComponentViewMap())
        {
            for (const auto& pin : view->getInputPins())
                require(pin.state == PinState::DISCONNECTED, "Blocked input displayed a signal.");
            for (const auto& pin : view->getOutputPins())
                require(pin.state == PinState::DISCONNECTED, "Blocked output displayed a signal.");
        }
        for (const auto& [id, net] : scene.getNets())
            require(net.getState() == PinState::DISCONNECTED, "Blocked net displayed a signal.");
        for (const auto& [id, wire] : scene.getWires())
            require(wire.getState() == PinState::DISCONNECTED, "Blocked wire displayed a signal.");
        require(
            scene.pinState({source, 0}, PinType::OUTPUT) == PinState::DISCONNECTED,
            "Blocked wire preview displayed an active signal."
        );

        scene.rebuildNets();
        require(
            scene.propagate() == EvalOrderResult::CYCLE_DETECTED,
            "Rebuilding geometry cleared an unresolved feedback error."
        );
        require(scene.removeWire(*feedbackId), "Could not remove the offending feedback wire.");
        require(scene.getRejectedConnections().empty(), "Repair retained stale rejection details.");
        require(scene.isSimulationDirty(), "Repaired scene did not request propagation.");
        require(scene.propagate() == EvalOrderResult::OK, "Repair did not resume simulation.");
        require(scene.getLastEvalResult() == EvalOrderResult::OK, "Repair kept the cycle warning.");
        scene.syncVisuals();
        require(!scene.getLogicComponent(second)->getStateOutPin(), "Repair lost the first edge.");
        require(!scene.getLogicComponent(fanout)->getStateOutPin(), "Repair lost valid fanout.");
        require(
            !scene.getLogicComponent(unrelated)->getStateOutPin(),
            "Repair did not restore the unrelated circuit."
        );
        require(scene.updateClocks(0.5f), "Repair did not resume automatic clocks.");
        for (const auto& [netId, net] : scene.getNets())
        {
            const auto driver = net.getDriver();
            if (!driver)
                continue;
            for (const PinRef& sink : net.getSinks())
            {
                const Connection edge{
                    driver->componentId, driver->pinIndex, sink.componentId, sink.pinIndex
                };
                const auto& incoming =
                    scene.getLogicComponent(sink.componentId)->getInConnections();
                const auto& outgoing =
                    scene.getLogicComponent(driver->componentId)->getOutConnections();
                require(
                    std::find(incoming.begin(), incoming.end(), edge) != incoming.end() &&
                        std::find(outgoing.begin(), outgoing.end(), edge) != outgoing.end(),
                    "Repaired net did not match the simulation graph."
                );
            }
        }

        scene.commitWire(feedback);
        require(
            scene.propagate() == EvalOrderResult::CYCLE_DETECTED, "Repeated feedback was lost."
        );
        scene.removeComponent(first);
        require(
            scene.propagate() == EvalOrderResult::OK, "Deleting the feedback gate did not repair."
        );
        require(
            scene.getLastEvalResult() == EvalOrderResult::OK, "Deletion retained an old error."
        );
    }
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

void geometryServices()
{
    std::map<WireId, Wire> wires{
        {4, route({0, 0}, {4, 0})}, {8, route({4, 0}, {8, 0})}, {12, route({20, 0}, {24, 0})}
    };
    Wire degenerate;
    degenerate.setPath({{30, 0}});
    wires.emplace(15, degenerate);
    WireId next = 16;
    const auto changes = normalizeWires(wires, {}, next);
    require(
        changes.removed == std::vector<WireId>{4, 8, 15} &&
            changes.added == std::vector<WireId>{16} && wires.contains(12) && next == 17,
        "Normalization did not report final identity changes."
    );
    require(
        wires.at(16).getPath() == std::vector<GridCoords>{{0, 0}, {8, 0}},
        "Degree-two seam was not healed."
    );
    const auto unchanged = normalizeWires(wires, {}, next);
    require(
        unchanged.removed.empty() && unchanged.added.empty() && next == 17,
        "Repeated normalization replaced settled identities."
    );

    const std::vector<PinAnchor> pins{{{2, 1}, PinType::OUTPUT, {4, 0}}};
    const auto split = normalizeWires(wires, pins, next);
    require(
        split.removed == std::vector<WireId>{16} && split.added.size() == 2,
        "Pin anchor did not protect a branch endpoint."
    );
    require(normalizeWires(wires, pins, next).added.empty(), "Pin seam was healed away.");
    wires.emplace(next++, route({4, 0}, {4, 4}));
    wires.begin()->second.setState(PinState::OFF);
    wires.rbegin()->second.setState(PinState::ON);
    auto junctions = wireJunctions(wires, {});
    require(
        junctions.size() == 1 && junctions[0].position == GridCoords{4, 0} &&
            junctions[0].state == PinState::ON && wireJunctions(wires, pins).empty(),
        "Junction geometry, state priority, or pin exclusion changed."
    );
    std::vector<ComponentGeometry> components{{2, {4, 0}, 0.2f, 0, 0.2f, 0.2f, pins}};
    require(
        hitGeometry(components, wires, 0.2f, 0, {4, 0}).type == HitType::COMPONENT_PIN,
        "Pin hit did not take priority over a wire junction and body."
    );
    require(
        hitGeometry({}, wires, 0.2f, 0, {4, 0}).type == HitType::WIRE_JUNCTION,
        "Shared wire endpoint hit changed."
    );
    require(
        hitGeometry({}, wires, 0.1f, 0, {2, 0}).type == HitType::WIRE_BODY,
        "Wire interior hit changed."
    );
    const auto body = hitGeometry(components, {}, 0.2f, 0.05f, {4, 1});
    require(
        body.type == HitType::COMPONENT_BODY && body.componentId == 2,
        "Component body hit lost its identity."
    );
    require(
        hitGeometry(components, {}, 0.295f, 0, {6, 0}).type == HitType::NONE,
        "Component body hit inset changed."
    );
    components.push_back({3, {20, 0}, 1, 0, 0.2f, 0.2f, pins});
    require(
        overlapsComponent(components, 2) && !overlapsComponent(components, 99),
        "Coincident pins or missing component overlap queries changed."
    );
}

void connectivityBuilder()
{
    Circuit circuit;
    const int latch = circuit.addLatch(LatchType::SR_LATCH);
    const int sink = circuit.addGate(NOT);
    std::map<WireId, Wire> wires{{7, route({0, 0}, {5, 0})}};
    std::vector<PinAnchor> pins{
        {{latch, 1}, PinType::OUTPUT, {0, 0}}, {{sink, 0}, PinType::INPUT, {5, 0}}
    };
    auto topology = buildConnectivity(wires, pins, circuit, 40);
    require(
        topology.status == EvalOrderResult::OK && topology.nextNetId == 41 &&
            topology.wireNets.at(7) == 40 && topology.pinNets.at({latch, 1, true}) == 40 &&
            topology.pinNets.at({sink, 0, false}) == 40,
        "Builder lost pin direction, output index, or allocator state."
    );
    require(
        wires.at(7).getNet() == INVALID_NET_ID &&
            wires.at(7).getPath() == std::vector<GridCoords>{{0, 0}, {5, 0}},
        "Connectivity builder mutated its geometry input."
    );
    require(
        circuit.propagate() == EvalOrderResult::OK && circuit.getComponent(sink)->getStateInPin(0),
        "Builder propagated Q instead of ~Q."
    );

    pins.push_back({{latch, 1}, PinType::INPUT, {5, 0}});
    topology = buildConnectivity(wires, pins, circuit, topology.nextNetId);
    require(
        topology.status == EvalOrderResult::CYCLE_DETECTED &&
            topology.rejectedConnections.size() == 1 &&
            topology.rejectedConnections[0].connection.srcPinIndex == 1 &&
            circuit.getComponent(sink)->getInConnections().empty() &&
            topology.pinNets.at({latch, 1, false}) == topology.pinNets.at({latch, 1, true}),
        "Rejected topology left a partial graph or merged directional pin identities."
    );
    pins.pop_back();
    topology = buildConnectivity(wires, pins, circuit, topology.nextNetId);
    require(
        topology.status == EvalOrderResult::OK && topology.rejectedConnections.empty() &&
            circuit.getComponent(sink)->getInConnections().size() == 1,
        "Repair did not rebuild the complete graph."
    );
    pins.push_back({{sink, 0}, PinType::OUTPUT, {0, 0}});
    topology = buildConnectivity(wires, pins, circuit, topology.nextNetId);
    require(
        topology.nets.begin()->second.shorted() && !topology.nets.begin()->second.hasDriver() &&
            circuit.getComponent(sink)->getInConnections().empty(),
        "Shorted net received an arbitrary driver."
    );

    // A crossing without a pin/endpoint is visual geometry, not an electrical junction.
    const std::map<WireId, Wire> crossing{{1, route({0, 0}, {8, 0})}, {2, route({4, -4}, {4, 4})}};
    topology = buildConnectivity(crossing, {}, circuit, topology.nextNetId);
    require(topology.nets.size() == 2, "Builder connected crossing interiors.");
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
        {"scene_wire_overlaps", sceneWireOverlaps},
        {"distinct_route_pins", distinctRoutePins},
        {"scene_connection_rejections", sceneConnectionRejections},
        {"source_components", sourceComponents},
        {"geometry_services", geometryServices},
        {"connectivity_builder", connectivityBuilder}
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
