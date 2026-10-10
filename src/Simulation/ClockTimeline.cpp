#include "Simulation/ClockTimeline.h"

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
        live.insert(reading.path);
        auto& trace = m_traces[reading.path];
        if (trace.empty() || trace.back().high != reading.high)
            trace.push_back({time, reading.high});
        while (trace.size() > sampleLimit)
            trace.pop_front();
    }
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
    m_steps.clear();
}
