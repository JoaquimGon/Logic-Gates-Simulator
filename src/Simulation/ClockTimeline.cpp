#include "Simulation/ClockTimeline.h"

#include "Simulation/Circuit.h"
#include "Simulation/Subcircuit.h"

#include <algorithm>
#include <set>


void ClockTimeline::record(double time, const std::vector<ClockReading>& readings, bool edge)
{
    if (time < m_time)
        clear();
    m_time = time;
    std::set<ClockPath> live;
    for (const auto& reading : readings)
    {
        if (!edge)
            live.insert(reading.path);
        auto& trace = m_traces[reading.path];
        if (trace.empty() || trace.back().high != reading.high)
            trace.push_back({time, reading.high});
        while (trace.size() > sampleLimit)
            trace.pop_front();
    }
    if (!edge)
        std::erase_if(m_traces, [&](const auto& row) { return !live.contains(row.first); });
    if (edge && (m_steps.empty() || m_steps.back() != time))
        m_steps.push_back(time);
    while (m_steps.size() > sampleLimit)
        m_steps.pop_front();
}


void ClockTimeline::clear()
{
    m_time = 0;
    m_traces.clear();
    for (auto& signal : m_signals)
    {
        signal.samples.clear();
        signal.shortestInterval = 0;
    }
    m_steps.clear();
}


namespace
{
bool sameSignal(const SignalTrace& a, const SignalTrace& b)
{
    return a.path == b.path && a.direction == b.direction && a.pinIndex == b.pinIndex;
}
} // namespace


bool ClockTimeline::tracesSignal(const SignalTrace& trace) const
{
    return std::any_of(
        m_signals.begin(), m_signals.end(), [&](const auto& current) { return sameSignal(current, trace); }
    );
}


bool ClockTimeline::toggleSignal(SignalTrace trace)
{
    const auto found = std::find_if(
        m_signals.begin(), m_signals.end(), [&](const auto& current) { return sameSignal(current, trace); }
    );
    if (found != m_signals.end())
        m_signals.erase(found);
    else if (m_signals.size() == signalLimit)
        return false;
    else
        m_signals.push_back(std::move(trace));
    return true;
}


void ClockTimeline::removeSignal(std::size_t index)
{
    if (index < m_signals.size())
        m_signals.erase(m_signals.begin() + index);
}


bool ClockTimeline::prepareSignals(std::uint64_t revision)
{
    if (m_signalRevision == revision)
        return false;
    if (m_signalRevision && *m_signalRevision != revision)
    {
        // Compiled IDs inside placed subcircuits can change on publication/undo.
        std::erase_if(m_signals, [](const auto& signal) { return signal.path.size() > 1; });
        for (auto& signal : m_signals)
        {
            signal.samples.clear();
            signal.shortestInterval = 0;
        }
    }
    m_signalRevision = revision;
    return true;
}


void ClockTimeline::recordSignals(const Circuit& circuit, bool available)
{
    if (m_signals.empty())
        return;
    available &= circuit.getLastEvalResult() == SimulationResult::OK && !circuit.isStateDirty();
    auto resolve = [&](const auto& signal) -> const Component*
    {
        const Circuit* current = &circuit;
        const Component* component = nullptr;
        for (std::size_t i = 0; i < signal.path.size(); ++i)
        {
            component = current->getComponent(signal.path[i]);
            if (!component)
                return nullptr;
            if (i + 1 < signal.path.size())
            {
                const auto* sub = dynamic_cast<const Subcircuit*>(component);
                if (!sub)
                    return nullptr;
                current = &sub->runtimeCircuit();
            }
        }
        if (!component || signal.pinIndex >= static_cast<unsigned int>(
                                                 signal.direction == PinType::INPUT ? component->getInputPinCount()
                                                                                    : component->getOutputPinCount()
                                             ))
            return nullptr;
        return component;
    };
    // Retain transitions and availability changes only; missing runtime paths invalidate their selection.
    for (auto it = m_signals.begin(); it != m_signals.end();)
    {
        auto& signal = *it;
        const auto* component = resolve(signal);
        if (!component)
        {
            it = m_signals.erase(it);
            continue;
        }
        const bool pinAvailable =
            available && (signal.direction == PinType::OUTPUT ||
                          std::any_of(
                              component->getInConnections().begin(), component->getInConnections().end(),
                              [&](const auto& edge) { return edge.destPinIndex == static_cast<int>(signal.pinIndex); }
                          ));
        const bool high =
            pinAvailable && (signal.direction == PinType::INPUT ? component->getStateInPin(signal.pinIndex)
                                                                : component->getStateOutPin(signal.pinIndex));
        auto& samples = signal.samples;
        if (samples.empty() || samples.back().high != high || samples.back().available != pinAvailable)
        {
            if (!samples.empty())
            {
                const double dt = circuit.clockTime() - samples.back().time;
                if (dt > 0 && (signal.shortestInterval == 0 || dt < signal.shortestInterval))
                    signal.shortestInterval = dt;
            }
            samples.push_back({circuit.clockTime(), high, pinAvailable});
        }
        while (samples.size() > sampleLimit)
            samples.pop_front();
        ++it;
    }
}
