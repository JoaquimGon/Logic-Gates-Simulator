#pragma once
#include "Simulation/ClockTimeline.h"
#include "Simulation/SimulationStatus.h"

#include <array>
#include <cmath>
#include <string>

namespace bench
{
inline std::string
propagationReason(SimulationResult result, std::size_t remaining, const SimulationWork& work)
{
    if (result == SimulationResult::OK)
        return "settled";
    if (remaining == 0)
        return "evaluation_budget_exhausted";
    if (work.rounds == 1024)
        return "round_limit_reached";
    return "component_failure";
}

using Pixel = std::array<unsigned char, 4>;

inline bool pixelMatches(const Pixel& actual, const Pixel& expected, int tolerance = 4)
{
    for (int channel = 0; channel < 3; ++channel)
        if (std::abs(int(actual[channel]) - int(expected[channel])) > tolerance)
            return false;
    return true;
}

// Compare the COMPLETE retained trace, including availability, compression and ordering.
inline bool
traceMatches(const std::deque<ClockSample>& actual, const std::deque<ClockSample>& expected)
{
    if (actual.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        if (std::abs(actual[i].time - expected[i].time) > 1e-10 || !std::isfinite(actual[i].time) ||
            actual[i].high != expected[i].high || actual[i].available != expected[i].available)
            return false;
        // Availability can legitimately change at the same simulated time.
        // Unexpected duplicates still fail exact length/time/value comparison.
        if (i && actual[i].time < actual[i - 1].time)
            return false;
    }
    return true;
}
} // namespace bench
