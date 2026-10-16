#pragma once

#include <cstddef>

enum class SimulationResult
{
    OK,
    NON_CONVERGENT,
    CONNECTION_REJECTED
};

enum class ClockWorkLimit
{
    None,
    TimeBudget,
    SliceLimit
};

/** @brief Work accumulated since the last read; diagnostic state is not saved or copied. */
struct SimulationWork
{
    double propagationMs = 0;
    std::size_t propagationCalls = 0, evaluations = 0, rounds = 0;
    std::size_t clockSlices = 0, edgeTimes = 0;
    ClockWorkLimit clockLimit = ClockWorkLimit::None;
};
