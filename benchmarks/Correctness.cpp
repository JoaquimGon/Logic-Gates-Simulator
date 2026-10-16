#include "Benchmark.h"
#include "Sha256.h"
#include "Simulation/Circuit.h"
#include "Validation.h"

#include <algorithm>

namespace bench
{
namespace
{
class Observer : public Component
{
  public:
    explicit Observer(Circuit& owner) : Component(-1, 1, 0), circuit(&owner) {}


    std::unique_ptr<Component> clone() const override { return std::make_unique<Observer>(*this); }


    void evaluate() override {}


    int clockInputPin() const override { return 0; }


    void onClockEdge(bool high) override { events.push_back({circuit->clockTime(), high}); }


    Circuit* circuit;
    std::deque<ClockSample> events;
};


/** @brief Drain clock catch-up work under a watchdog so timing fixtures cannot wait indefinitely. */
void drain(Circuit& circuit, ClockTimeline& timeline, float duration)
{
    const auto start = Clock::now();
    do
    {
        circuit.updateClocks(duration, &timeline);
        duration = 0;
        check(milliseconds(start) < 10000, "Correctness/warmup clock drain watchdog expired.");
    } while (circuit.pendingClockTime() > 0);
    check(circuit.getLastEvalResult() == SimulationResult::OK, "Clock correctness propagation failed.");
}


std::deque<ClockSample> expectedTrace(int edges, int stride, bool inverse, bool initial)
{
    std::deque<ClockSample> samples;
    if (initial)
        samples.push_back({0, inverse});
    for (int edge = 1; edge <= edges / stride; ++edge)
        samples.push_back({double(edge * stride) / 256, bool(edge % 2) != inverse});
    return samples;
}


void retain(std::deque<ClockSample>& samples)
{
    while (samples.size() > ClockTimeline::sampleLimit)
        samples.pop_front();
}
} // namespace


void validatorChecks(Recorder& results)
{
    check(
        sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Empty SHA-256 vector failed."
    );
    check(
        sha256({'a', 'b', 'c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 abc vector failed."
    );
    check(
        sha256(std::vector<unsigned char>(1000000, 'a')) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
        "Multi-block SHA-256 vector failed."
    );
    const Pixel blue{51, 128, 230, 255};
    check(
        pixelMatches(blue, blue) && !pixelMatches({26, 26, 26, 255}, blue) &&
            !pixelMatches({0, 255, 0, 255}, {255, 0, 0, 255}),
        "Pixel validator accepts background/wrong state."
    );
    const std::deque<ClockSample> expected{{0, false}, {.5, true}, {1, false}};
    check(traceMatches(expected, expected), "Trace validator rejects correct transitions.");
    auto corrupt = expected;
    corrupt.erase(corrupt.begin() + 1);
    check(!traceMatches(corrupt, expected), "Trace validator accepted a lost transition.");
    corrupt = expected;
    corrupt.insert(corrupt.begin() + 1, expected[1]);
    check(!traceMatches(corrupt, expected), "Trace validator accepted a duplicate transition.");
    corrupt = expected;
    std::swap(corrupt[1], corrupt[2]);
    check(!traceMatches(corrupt, expected), "Trace validator accepted out-of-order transitions.");
    corrupt = expected;
    corrupt[1].time += .01;
    check(!traceMatches(corrupt, expected), "Trace validator accepted the wrong timestamp.");
    corrupt = expected;
    corrupt[1].high = false;
    check(!traceMatches(corrupt, expected), "Trace validator accepted the wrong state.");
    corrupt = expected;
    corrupt[1].available = false;
    check(!traceMatches(corrupt, expected), "Trace validator accepted wrong availability.");
    Circuit settled;
    settled.addInputPin();
    std::size_t remaining = 100000;
    auto result = settled.propagate(remaining);
    check(propagationReason(result, remaining, settled.takeSimulationWork()) == "settled", "Wrong settled diagnosis.");
    Circuit exhausted;
    exhausted.addInputPin();
    exhausted.addGate(NOT);
    remaining = 1;
    result = exhausted.propagate(remaining);
    check(
        propagationReason(result, remaining, exhausted.takeSimulationWork()) == "evaluation_budget_exhausted",
        "Wrong evaluation-budget diagnosis."
    );
    Circuit oscillating;
    const int gate = oscillating.addGate(NOT);
    check(oscillating.connectComponents(gate, 0, gate, 0), "Cannot create round-limit validator fixture.");
    remaining = 100000;
    result = oscillating.propagate(remaining);
    check(
        propagationReason(result, remaining, oscillating.takeSimulationWork()) == "round_limit_reached",
        "Wrong round-limit diagnosis."
    );
    results.outcome(
        "validator_negative_controls", 0, "pass",
        "Known SHA vectors; missing/duplicate/unordered/wrong-time/state/availability traces; "
        "background/wrong-state pixels; actual settled/evaluation/round-limit fixtures."
    );
}


void clockCorrectness(Recorder& results)
{
    // Separate untimed oracle run: inspect every settled intermediate gate state.
    // Two fast clocks share edges; the slower clock also coincides every second edge.
    for (const bool bulk : {false, true})
    {
        Circuit circuit;
        ClockTimeline timeline;
        std::vector<int> clocks, gates, observers;
        const std::array strides{2, 1, 1};
        for (int i = 0; i < 3; ++i)
        {
            clocks.push_back(circuit.addClock(float(128 / strides[i])));
            gates.push_back(circuit.addGate(NOT));
            observers.push_back(circuit.addComponent(std::make_unique<Observer>(circuit)));
            check(
                circuit.connectComponents(clocks.back(), 0, gates.back(), 0) &&
                    circuit.connectComponents(clocks.back(), 0, observers.back(), 0),
                "Invalid multi-clock fixture."
            );
            check(
                timeline.toggleSignal({{gates.back()}, PinType::OUTPUT, 0, "", "inverse", {}}),
                "Cannot trace multi-clock gate."
            );
        }
        check(circuit.propagate() == SimulationResult::OK, "Multi-clock fixture did not settle.");
        constexpr int edges = 2304; // Cross the 2048-sample retention limit for clocks/signals/steps.
        if (bulk)
            drain(circuit, timeline, float(edges) / 256);
        else
            for (int edge = 1; edge <= edges; ++edge)
            {
                drain(circuit, timeline, 1.0f / 256);
                check(std::abs(circuit.clockTime() - double(edge) / 256) < 1e-10, "Multi-clock time mismatch.");
                for (int i = 0; i < 3; ++i)
                {
                    const bool high = (edge / strides[i]) % 2;
                    check(
                        circuit.getComponent(clocks[i])->getStateOutPin() == high &&
                            circuit.getComponent(gates[i])->getStateOutPin() != high,
                        "Incorrect intermediate clock/gate state."
                    );
                }
            }
        for (int i = 0; i < 3; ++i)
        {
            const auto expected = expectedTrace(edges, strides[i], false, false);
            const auto* observer = static_cast<const Observer*>(circuit.getComponent(observers[i]));
            check(traceMatches(observer->events, expected), "Lost, duplicated or unordered multi-clock edge.");
            auto clockExpected = expectedTrace(edges, strides[i], false, true);
            auto signalExpected = expectedTrace(edges, strides[i], true, true);
            retain(clockExpected);
            retain(signalExpected);
            check(traceMatches(timeline.traces().at({clocks[i]}), clockExpected), "Clock trace/retention mismatch.");
            const auto& signal = timeline.signals()[i];
            check(traceMatches(signal.samples, signalExpected), "Probe trace/retention mismatch.");
            check(
                std::abs(signal.shortestInterval - double(strides[i]) / 256) < 1e-10,
                "Probe shortest-interval mismatch."
            );
        }
        check(timeline.steps().size() == ClockTimeline::sampleLimit, "Incorrect simultaneous-edge retention.");
        for (std::size_t i = 0; i < timeline.steps().size(); ++i)
            check(
                std::abs(timeline.steps()[i] - double(edges - int(ClockTimeline::sampleLimit) + 1 + int(i)) / 256) <
                    1e-10,
                "Simultaneous clock edge duplicated/lost in step history."
            );
        const auto snapshot = timeline.traces();
        for (int i = 0; i < 8; ++i)
            drain(circuit, timeline, 0);
        for (const auto& [path, samples] : snapshot)
            check(traceMatches(timeline.traces().at(path), samples), "Unchanged clock samples were not compressed.");
        // Availability changes are retained even without a value change; repeated unavailability
        // compresses.
        auto expectedAvailability = timeline.signals()[0].samples;
        expectedAvailability.push_back({circuit.clockTime(), false, false});
        retain(expectedAvailability);
        timeline.recordSignals(circuit, false);
        check(traceMatches(timeline.signals()[0].samples, expectedAvailability), "Incorrect unavailable probe trace.");
        const auto unavailable = timeline.signals()[0].samples;
        timeline.recordSignals(circuit, false);
        check(
            !unavailable.back().available && timeline.signals()[0].samples.size() == unavailable.size(),
            "Probe availability/compression mismatch."
        );
        expectedAvailability.push_back({circuit.clockTime(), true, true});
        retain(expectedAvailability);
        timeline.recordSignals(circuit, true);
        check(
            traceMatches(timeline.signals()[0].samples, expectedAvailability), "Incorrect restored probe availability."
        );
        results.outcome(
            bulk ? "clock_multiclock_bulk_correctness" : "clock_multiclock_correctness", 9, "pass",
            "2304 distinct edge times; 64/128/128 Hz; every intermediate output; complete retained "
            "clocks/probes/steps; simultaneous edges, compression and availability. Untimed oracle "
            "workload."
        );
    }
}
} // namespace bench
