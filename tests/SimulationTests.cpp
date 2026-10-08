#include "Simulation/Circuit.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// A model-only edge receiver verifies the simulator contract without adding a new app component.
class EdgeMemory : public Component
{
  public:
    explicit EdgeMemory(bool rising = true) : Component(-1, 2, 1), rising(rising) {}

    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<EdgeMemory>(*this);
    }

    void evaluate() override {}

    int clockInputPin() const override { return 1; }

    void onClockEdge(bool state) override
    {
        edges.push_back(state);
        samples.push_back(getStateInPin(0));
        if (state == rising)
            setStateOutPin(0, getStateInPin(0));
    }

    bool rising;
    std::vector<bool> edges, samples;
};

void cyclicPropagation()
{
    Circuit circuit;
    const int set = circuit.addInputPin(true), reset = circuit.addInputPin();
    const int q = circuit.addGate(NOR), notQ = circuit.addGate(NOR);
    require(
        circuit.connectComponents(reset, 0, q, 0) && circuit.connectComponents(notQ, 0, q, 1) &&
            circuit.connectComponents(set, 0, notQ, 0) && circuit.connectComponents(q, 0, notQ, 1),
        "Cross-coupled feedback was rejected."
    );
    auto check = [&](bool expected)
    {
        require(
            circuit.propagate() == SimulationResult::OK &&
                circuit.getComponent(q)->getStateOutPin() == expected &&
                circuit.getComponent(notQ)->getStateOutPin() != expected,
            "Gate-built SR memory did not settle or retain its state."
        );
    };
    check(true);
    static_cast<InputPin*>(circuit.getComponent(set))->toggle();
    check(true);
    static_cast<InputPin*>(circuit.getComponent(reset))->toggle();
    check(false);
    static_cast<InputPin*>(circuit.getComponent(reset))->toggle();
    check(false);
    Circuit restored = circuit;
    require(
        restored.propagate() == SimulationResult::OK && !restored.getComponent(q)->getStateOutPin(),
        "Circuit copy lost stable feedback state."
    );

    const int oscillator = circuit.addGate(NOT);
    const int clock = circuit.addClock();
    require(circuit.connectComponents(oscillator, 0, oscillator, 0), "Self-feedback was rejected.");
    require(
        circuit.propagate() == SimulationResult::NON_CONVERGENT &&
            circuit.unsettledComponent() == oscillator && !circuit.isStateDirty() &&
            !circuit.updateClocks(0.5f) && !circuit.getComponent(clock)->getStateOutPin(),
        "Oscillation did not stop automatic simulation within its bound."
    );
    const Circuit failedCopy = circuit;
    require(
        failedCopy.unsettledComponent() == oscillator,
        "Circuit copy lost the pending-activity diagnostic."
    );
    circuit.disconnectComponents(oscillator, 0, oscillator, 0);
    require(
        circuit.propagate() == SimulationResult::OK && circuit.unsettledComponent() == -1 &&
            circuit.updateClocks(0.5f),
        "Feedback repair did not restore clocks and propagation."
    );

    Circuit held;
    const int force = held.addInputPin(), memory = held.addGate(OR);
    require(
        held.connectComponents(memory, 0, memory, 0) && held.connectComponents(force, 0, memory, 1),
        "Stable self-loop rejected."
    );
    require(held.propagate() == SimulationResult::OK, "Stable low loop failed.");
    static_cast<InputPin*>(held.getComponent(force))->toggle();
    held.propagate();
    static_cast<InputPin*>(held.getComponent(force))->toggle();
    require(
        held.propagate() == SimulationResult::OK && held.getComponent(memory)->getStateOutPin(),
        "Stable self-feedback did not retain the forced high state."
    );
}

void clockEdgeTiming()
{
    Circuit circuit;
    const int slow = circuit.addClock(1), fast = circuit.addClock(2);
    const int receiver = circuit.addComponent(std::make_unique<EdgeMemory>());
    circuit.connectComponents(slow, 0, receiver, 0);
    circuit.connectComponents(fast, 0, receiver, 1);
    require(circuit.propagate() == SimulationResult::OK, "Clock fixture did not initialize.");
    auto* memory = static_cast<EdgeMemory*>(circuit.getComponent(receiver));
    require(memory->edges.empty(), "Wiring invented an initial clock transition.");
    require(circuit.updateClocks(1), "Long frame produced no clock edges.");
    require(
        memory->edges == std::vector<bool>{true, false, true, false} &&
            memory->samples == std::vector<bool>{false, true, true, false},
        "Multiple clocks skipped edges, used the wrong order, or delivered simultaneous edges "
        "separately."
    );
    const auto count = memory->edges.size();
    circuit.propagate();
    require(memory->edges.size() == count, "A second propagation replayed old clock edges.");

    Circuit backlog;
    const int clock = backlog.addClock(1000);
    const int probe = backlog.addComponent(std::make_unique<EdgeMemory>());
    backlog.connectComponents(clock, 0, probe, 1);
    backlog.updateClocks(0.5f);
    auto* trace = static_cast<EdgeMemory*>(backlog.getComponent(probe));
    require(!trace->edges.empty() && trace->edges.size() <= 256, "Clock catch-up was unbounded.");
    Circuit saved = backlog;
    for (int i = 0; i < 100 && trace->edges.size() < 1000; ++i)
        backlog.updateClocks(0);
    auto* savedTrace = static_cast<EdgeMemory*>(saved.getComponent(probe));
    for (int i = 0; i < 100 && savedTrace->edges.size() < 1000; ++i)
        saved.updateClocks(0);
    require(
        trace->edges.size() == 1000 && savedTrace->edges == trace->edges &&
            !backlog.getComponent(clock)->getStateOutPin(),
        "Bounded catch-up or snapshot copy lost pending edges/clock phase."
    );
    auto* source = static_cast<Clock*>(backlog.getComponent(clock));
    source->setPaused(true);
    require(!backlog.updateClocks(2), "Paused clock accumulated new edges.");
    source->step();
    backlog.markStateDirty();
    backlog.propagate();
    require(trace->edges.size() == 1001 && trace->edges.back(), "Manual step omitted its edge.");
    require(!backlog.updateClocks(0), "Manual step was replayed by clock catch-up.");

    for (float delta :
         {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        bool rejected = false;
        try
        {
            backlog.updateClocks(delta);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Invalid elapsed time corrupted the clock scheduler.");
    }
}

void clockEdgeReceivers()
{
    Circuit circuit;
    const int data = circuit.addInputPin(true), clock = circuit.addClock();
    const int rising = circuit.addComponent(std::make_unique<EdgeMemory>());
    const int falling = circuit.addComponent(std::make_unique<EdgeMemory>(false));
    for (int id : {rising, falling})
    {
        circuit.connectComponents(data, 0, id, 0);
        circuit.connectComponents(clock, 0, id, 1);
    }
    circuit.updateClocks(0.5f);
    require(
        circuit.getComponent(rising)->getStateOutPin() &&
            !circuit.getComponent(falling)->getStateOutPin(),
        "Rising/falling edges were confused."
    );
    circuit.updateClocks(0.5f);
    require(circuit.getComponent(falling)->getStateOutPin(), "Falling edge did not sample data.");
    static_cast<InputPin*>(circuit.getComponent(data))->toggle();
    circuit.propagate();
    require(
        circuit.getComponent(rising)->getStateOutPin() &&
            circuit.getComponent(falling)->getStateOutPin(),
        "Edge receiver became a level-sensitive latch."
    );
    circuit.updateClocks(0.5f);
    require(
        !circuit.getComponent(rising)->getStateOutPin() &&
            circuit.getComponent(falling)->getStateOutPin(),
        "Next rising edge did not sample updated data."
    );

    for (bool reverse : {false, true})
    {
        Circuit chain;
        const int input = chain.addInputPin(true), pulse = chain.addClock();
        const int a = chain.addComponent(std::make_unique<EdgeMemory>());
        const int b = chain.addComponent(std::make_unique<EdgeMemory>());
        const int first = reverse ? b : a, second = reverse ? a : b;
        chain.connectComponents(input, 0, first, 0);
        chain.connectComponents(first, 0, second, 0);
        chain.connectComponents(pulse, 0, first, 1);
        chain.connectComponents(pulse, 0, second, 1);
        chain.updateClocks(0.5f);
        require(
            chain.getComponent(first)->getStateOutPin() &&
                !chain.getComponent(second)->getStateOutPin(),
            "One edge advanced two register stages because of component order."
        );
        chain.updateClocks(1);
        require(
            chain.getComponent(second)->getStateOutPin(),
            "Second rising edge failed to advance the chain."
        );
    }
    Circuit generated;
    const int manual = generated.addInputPin(), invert = generated.addGate(NOT);
    const int receiver = generated.addComponent(std::make_unique<EdgeMemory>());
    generated.connectComponents(manual, 0, invert, 0);
    generated.connectComponents(invert, 0, receiver, 1);
    generated.propagate();
    auto* memory = static_cast<EdgeMemory*>(generated.getComponent(receiver));
    static_cast<InputPin*>(generated.getComponent(manual))->toggle();
    generated.propagate();
    require(
        memory->edges == std::vector<bool>{true, false},
        "Gate-generated clock transitions did not reach the receiver."
    );

    // Existing level-sensitive latches can form a falling-edge master/slave circuit.
    Circuit native;
    const int signal = native.addInputPin(true), pulse = native.addClock();
    const int inverse = native.addGate(NOT);
    const int master = native.addLatch(LatchType::D_LATCH),
              slave = native.addLatch(LatchType::D_LATCH);
    native.connectComponents(signal, 0, master, 0);
    native.connectComponents(pulse, 0, master, 1);
    native.connectComponents(pulse, 0, inverse, 0);
    native.connectComponents(master, 0, slave, 0);
    native.connectComponents(inverse, 0, slave, 1);
    native.propagate();
    native.updateClocks(0.5f);
    require(
        native.getComponent(master)->getStateOutPin() &&
            !native.getComponent(slave)->getStateOutPin(),
        "Native master/slave circuit transferred data on the wrong edge."
    );
    native.updateClocks(0.5f);
    require(native.getComponent(slave)->getStateOutPin(), "Native slave missed the falling edge.");
    static_cast<InputPin*>(native.getComponent(signal))->toggle();
    native.propagate();
    native.updateClocks(0.5f);
    require(
        native.getComponent(slave)->getStateOutPin(), "Native slave changed during the high phase."
    );
    native.updateClocks(0.5f);
    require(
        !native.getComponent(slave)->getStateOutPin(), "Native slave missed the next falling edge."
    );
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::pair<const char*, void (*)()> tests[] = {
            {"cyclic_propagation", cyclicPropagation},
            {"clock_edge_timing", clockEdgeTiming},
            {"clock_edge_receivers", clockEdgeReceivers}
        };
        bool matched = false;
        for (const auto& [name, run] : tests)
            if (argc == 1 || std::string(argv[1]) == name)
            {
                run();
                matched = true;
                std::cout << "PASS: " << name << '\n';
            }
        require(matched, "Unknown simulation test.");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
