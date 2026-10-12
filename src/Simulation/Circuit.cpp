#include "Circuit.h"

#include "Simulation/Subcircuit.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

Circuit::Circuit(const Circuit& other)
    : m_componentOrder(other.m_componentOrder), m_outputStates(other.m_outputStates),
      m_currentId(other.m_currentId), m_structureDirty(other.m_structureDirty),
      m_stateDirty(other.m_stateDirty), m_pendingClockTime(other.m_pendingClockTime),
      m_clockTime(other.m_clockTime), m_clocksPaused(other.m_clocksPaused),
      m_lastPropagateDurationMs(other.m_lastPropagateDurationMs),
      m_lastEvalResult(other.m_lastEvalResult), m_unsettledComponent(other.m_unsettledComponent)
{
    for (const auto& [id, component] : other.m_components)
        m_components.emplace(id, component->clone());
}

Circuit& Circuit::operator=(const Circuit& other)
{
    if (this != &other)
    {
        Circuit copy(other);
        *this = std::move(copy);
    }
    return *this;
}

void Circuit::preserveAllocatedIds(const Circuit& other)
{
    m_currentId = std::max(m_currentId, other.m_currentId);
}

void Circuit::resizeGateInputs(int componentId, int inputPinCount)
{
    auto* gate = dynamic_cast<Gate*>(getComponent(componentId));
    if (!gate || !Gate::isValidInputPinCount(gate->getType(), inputPinCount))
        throw std::invalid_argument("Invalid native gate input count.");
    const auto connections = gate->getInConnections();
    for (const auto& edge : connections)
        if (edge.destPinIndex >= inputPinCount)
            disconnectComponents(
                edge.srcComponentId, edge.srcPinIndex, componentId, edge.destPinIndex
            );
    gate->m_stateInPins.resize(inputPinCount, false);
    gate->m_invertedInputs.resize(inputPinCount, false);
    m_structureDirty = true;
    m_stateDirty = true;
}

int Circuit::addGate(GateType type)
{
    return addGate(type, type == NOT ? 1 : 2);
}

int Circuit::addGate(GateType type, int inputPinCount)
{
    return addComponent(std::make_unique<Gate>(-1, type, inputPinCount));
}

int Circuit::addInputPin(bool initialState)
{
    return addComponent(std::make_unique<InputPin>(-1, initialState));
}

int Circuit::addOutputPin()
{
    return addComponent(std::make_unique<OutputPin>(-1));
}

Component* Circuit::getComponent(int id)
{
    auto it = m_components.find(id);
    return it != m_components.end() ? it->second.get() : nullptr;
}

const Component* Circuit::getComponent(int id) const
{
    auto it = m_components.find(id);
    return it != m_components.end() ? it->second.get() : nullptr;
}

void Circuit::delComponent(int id)
{
    Component* comp = getComponent(id);
    if (!comp)
        return;

    for (const auto& connection : comp->getInConnections())
        if (Component* src = getComponent(connection.srcComponentId))
            src->delOutConnection(connection);

    for (const auto& connection : comp->getOutConnections())
    {
        if (Component* dest = getComponent(connection.destComponentId))
        {
            dest->delInConnection(connection);
            dest->setStateInPin(connection.destPinIndex, false);
        }
    }

    m_components.erase(id);
    m_outputStates.erase(id);
    m_structureDirty = true;
    m_stateDirty = true;
}

bool Circuit::connectComponents(
    int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex
)
{
    return tryConnectComponents(srcComponentId, srcPinIndex, destComponentId, destPinIndex) ==
           ConnectionResult::OK;
}

ConnectionResult Circuit::tryConnectComponents(
    int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex
)
{
    Component* src = getComponent(srcComponentId);
    Component* dest = getComponent(destComponentId);
    if (!src || !dest)
        return ConnectionResult::INVALID_COMPONENT;

    if (srcPinIndex < 0 || srcPinIndex >= src->getOutputPinCount() || destPinIndex < 0 ||
        destPinIndex >= dest->getInputPinCount())
    {
        return ConnectionResult::INVALID_PIN;
    }

    const Connection connection{srcComponentId, srcPinIndex, destComponentId, destPinIndex};
    for (const auto& existing : dest->getInConnections())
    {
        if (existing.destPinIndex == destPinIndex)
            return existing == connection ? ConnectionResult::OK
                                          : ConnectionResult::INPUT_ALREADY_DRIVEN;
    }

    src->addOutConnection(connection);
    dest->addInConnection(connection);
    // Wiring initializes a level, rather than inventing a clock edge.
    dest->setStateInPin(destPinIndex, src->getStateOutPin(srcPinIndex));
    m_structureDirty = true;
    m_stateDirty = true;
    return ConnectionResult::OK;
}

void Circuit::disconnectComponents(
    int srcComponentId, int srcPinIndex, int destComponentId, int destPinIndex
)
{
    Component* src = getComponent(srcComponentId);
    Component* dest = getComponent(destComponentId);
    if (!src || !dest)
        return;

    const Connection connection{srcComponentId, srcPinIndex, destComponentId, destPinIndex};
    const auto& outgoing = src->getOutConnections();
    if (std::find(outgoing.begin(), outgoing.end(), connection) == outgoing.end())
        return;

    src->delOutConnection(connection);
    dest->delInConnection(connection);
    dest->setStateInPin(destPinIndex, false);

    m_structureDirty = true;
    m_stateDirty = true;
}

void Circuit::clearConnections()
{
    for (auto& [id, component] : m_components)
    {
        component->clearConnections();
        for (int pin = 0; pin < component->getInputPinCount(); ++pin)
            component->setStateInPin(pin, false);
    }

    m_structureDirty = true;
    m_stateDirty = true;
}

int Circuit::addComponent(std::unique_ptr<Component> component)
{
    if (!component)
        throw std::invalid_argument("Cannot add a null component.");
    const int id = m_currentId++;
    component->m_id = id;
    m_components.emplace(id, std::move(component));
    m_structureDirty = m_stateDirty = true;
    return id;
}

int Circuit::addClock(float frequencyHz)
{
    return addComponent(std::make_unique<Clock>(-1, frequencyHz));
}

void Circuit::replaceComponent(int id, std::unique_ptr<Component> component)
{
    if (!component || !m_components.contains(id))
        throw std::invalid_argument("Cannot replace a missing component.");
    delComponent(id); // Remove both endpoints' old edges; Scene rebuilds them from its wires.
    component->m_id = id;
    m_components.emplace(id, std::move(component));
}

void Circuit::collectClocks(std::vector<Clock*>& clocks)
{
    std::vector<int> ids;
    for (const auto& [id, component] : m_components)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (int id : ids)
    {
        auto* component = getComponent(id);
        if (auto* clock = dynamic_cast<Clock*>(component))
            clocks.push_back(clock);
        else
            component->collectClocks(clocks);
    }
}

int Circuit::addLatch(LatchType type)
{
    return addComponent(std::make_unique<Latch>(-1, type));
}

void Circuit::publishOutputs(const std::vector<int>& components, std::vector<int>& changed)
{
    struct InputChange
    {
        int component, pin;
        bool value;
    };

    std::vector<InputChange> inputs;
    for (int id : components)
    {
        auto* source = getComponent(id);
        auto& previous = m_outputStates[id];
        const bool initial = previous.empty();
        if (initial)
            previous.resize(source->getOutputPinCount());
        for (int pin = 0; pin < source->getOutputPinCount(); ++pin)
        {
            const bool value = source->getStateOutPin(pin);
            if (!initial && previous[pin] == value)
                continue;
            previous[pin] = value;
            for (const auto& edge : source->getOutConnections())
                if (edge.srcPinIndex == pin &&
                    getComponent(edge.destComponentId)->getStateInPin(edge.destPinIndex) != value)
                    inputs.push_back({edge.destComponentId, edge.destPinIndex, value});
        }
    }
    // Deliver the whole step before invoking receivers; callbacks do not publish outputs yet.
    for (const auto& input : inputs)
    {
        getComponent(input.component)->setStateInPin(input.pin, input.value);
        changed.push_back(input.component);
    }
    for (const auto& input : inputs)
    {
        auto* receiver = getComponent(input.component);
        if (receiver->clockInputPin() == input.pin)
            receiver->onClockEdge(input.value);
    }
}

SimulationResult Circuit::propagate()
{
    std::size_t remaining = 100000;
    return propagate(remaining);
}

SimulationResult Circuit::propagate(std::size_t& remainingEvaluations)
{
    const auto start = std::chrono::steady_clock::now();
    const auto initialBudget = remainingEvaluations;
    ++m_work.propagationCalls;
    m_unsettledComponent = -1;
    if (m_structureDirty)
    {
        m_componentOrder.clear();
        for (const auto& [id, component] : m_components)
            m_componentOrder.push_back(id);
        std::sort(m_componentOrder.begin(), m_componentOrder.end());
        m_structureDirty = false;
    }
    // Sources can change outside evaluate() (manual inputs, clocks, restored state).
    std::vector<int> dirty = m_componentOrder, next;
    publishOutputs(m_componentOrder, next);
    constexpr int maxSteps = 1024;
    bool failed = false;
    int steps = 0;
    while (!dirty.empty() && steps++ < maxSteps && !failed)
    {
        ++m_work.rounds;
        next.clear();
        for (int id : dirty)
        {
            if (remainingEvaluations == 0)
            {
                failed = true;
                m_unsettledComponent = id;
                break;
            }
            --remainingEvaluations;
            auto* component = getComponent(id);
            component->evaluate(remainingEvaluations);
            if (component->simulationResult() != SimulationResult::OK)
            {
                failed = true;
                m_unsettledComponent = id;
                break;
            }
        }
        if (failed)
            break;
        publishOutputs(dirty, next);
        std::sort(next.begin(), next.end());
        next.erase(std::unique(next.begin(), next.end()), next.end());
        dirty.swap(next);
    }
    m_lastEvalResult =
        !failed && dirty.empty() ? SimulationResult::OK : SimulationResult::NON_CONVERGENT;
    if (m_lastEvalResult != SimulationResult::OK && m_unsettledComponent == -1 && !dirty.empty())
        m_unsettledComponent = dirty.front();
    m_stateDirty = false;
    m_lastPropagateDurationMs =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    m_work.propagationMs += m_lastPropagateDurationMs;
    // The shared budget includes evaluations inside nested subcircuits, without timing twice.
    m_work.evaluations += initialBudget - remainingEvaluations;
    return m_lastEvalResult;
}

SimulationWork Circuit::takeSimulationWork()
{
    return std::exchange(m_work, {});
}

std::vector<ClockReading> Circuit::clockReadings(ClockPath prefix) const
{
    std::vector<ClockReading> readings;
    std::vector<int> ids;
    for (const auto& [id, component] : m_components)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (int id : ids)
    {
        const auto* component = getComponent(id);
        const auto* clock = dynamic_cast<const Clock*>(component);
        const auto* sub = dynamic_cast<const Subcircuit*>(component);
        if (!clock && !sub)
            continue;
        auto path = prefix;
        path.push_back(id);
        if (clock)
            readings.push_back(
                {path, clock->getFrequency(), clock->isPaused(), clock->getStateOutPin()}
            );
        else if (sub)
        {
            auto nested = sub->runtimeCircuit().clockReadings(path);
            readings.insert(readings.end(), nested.begin(), nested.end());
        }
    }
    return readings;
}

void Circuit::resetSources()
{
    std::vector<Clock*> clocks;
    collectClocks(clocks);
    for (auto* clock : clocks)
        clock->resetPhase();
    for (auto& [id, component] : m_components)
        if (auto* input = dynamic_cast<InputPin*>(component.get()))
            input->setState(false);
    m_clockTime = m_pendingClockTime = 0;
    m_work = {};
    markStateDirty();
}

void Circuit::setClocksPaused(bool paused)
{
    m_clocksPaused = paused;
    if (paused)
        m_pendingClockTime = 0;
}

namespace
{
void recordClocks(
    ClockTimeline* timeline,
    double time,
    std::vector<ClockReading>& readings,
    const std::vector<Clock*>& clocks,
    bool edge = false
)
{
    if (!timeline)
        return;
    // Both traversals use the same deterministic native/nested component order.
    for (std::size_t i = 0; i < readings.size(); ++i)
        readings[i].high = clocks[i]->getStateOutPin();
    timeline->record(time, readings, edge);
}
} // namespace

int Circuit::stepClocks(int count, ClockTimeline* timeline)
{
    if (count < 1 || count > 2)
        throw std::invalid_argument("Clock stepping supports one or two edge times.");
    setClocksPaused(true);
    m_work.clockLimit = ClockWorkLimit::None;
    if (isStateDirty())
        propagate();
    auto readings = timeline ? clockReadings() : std::vector<ClockReading>{};
    if (timeline)
        timeline->record(m_clockTime, readings);
    std::vector<Clock*> clocks;
    collectClocks(clocks);
    int processed = 0;
    while (processed < count && m_lastEvalResult == SimulationResult::OK)
    {
        double elapsed = std::numeric_limits<double>::infinity();
        for (const auto* clock : clocks)
            elapsed = std::min(elapsed, clock->timeUntilEdge());
        if (!std::isfinite(elapsed))
            break;
        bool edge = false;
        for (auto* clock : clocks)
            edge |= clock->advanceSlice(elapsed);
        m_clockTime += elapsed;
        if (!edge)
            break;
        ++processed;
        ++m_work.clockSlices;
        ++m_work.edgeTimes;
        propagate();
        if (timeline)
            recordClocks(timeline, m_clockTime, readings, clocks, true);
    }
    return processed;
}

bool Circuit::updateClocks(float deltaTime, ClockTimeline* timeline)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0)
        throw std::invalid_argument("Simulation elapsed time must be finite and non-negative.");
    m_work.clockLimit = ClockWorkLimit::None;
    if (isStateDirty())
        propagate();
    auto readings = timeline ? clockReadings() : std::vector<ClockReading>{};
    if (timeline)
        timeline->record(m_clockTime, readings);
    if (m_lastEvalResult != SimulationResult::OK || m_clocksPaused)
    {
        m_pendingClockTime = 0;
        return false;
    }
    std::vector<Clock*> clocks;
    collectClocks(clocks);
    m_pendingClockTime += deltaTime;
    bool changed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
    for (int step = 0; step < 256 && m_pendingClockTime > 0; ++step)
    {
        ++m_work.clockSlices;
        double nextEdge = std::numeric_limits<double>::infinity();
        for (const auto* clock : clocks)
            nextEdge = std::min(nextEdge, clock->timeUntilEdge());
        if (!std::isfinite(nextEdge))
        {
            m_pendingClockTime = 0;
            break;
        }
        const double elapsed = std::min(nextEdge, m_pendingClockTime);
        bool edgeOccurred = false;
        for (auto* clock : clocks)
            edgeOccurred |= clock->advanceSlice(elapsed);
        m_clockTime += elapsed;
        m_pendingClockTime = std::max(0.0, m_pendingClockTime - elapsed);
        if (edgeOccurred)
        {
            ++m_work.edgeTimes;
            changed = true;
            propagate();
            if (timeline)
                recordClocks(timeline, m_clockTime, readings, clocks, true);
            if (m_lastEvalResult != SimulationResult::OK)
            {
                m_pendingClockTime = 0;
                break;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            if (m_pendingClockTime > 0)
                m_work.clockLimit = ClockWorkLimit::TimeBudget;
            break;
        }
    }
    if (m_pendingClockTime > 0 && m_work.clockLimit == ClockWorkLimit::None)
        m_work.clockLimit = ClockWorkLimit::SliceLimit;
    if (timeline)
        recordClocks(timeline, m_clockTime, readings, clocks);
    return changed;
}
