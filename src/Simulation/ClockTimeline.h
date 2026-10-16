#pragma once
#include "Components/PinTypes.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

using ClockPath = std::vector<int>;

/** @brief Circuit-local IDs identify a native clock or a clock inside nested live instances. */
struct ClockReading
{
    ClockPath path;
    float frequency;
    bool paused, high;
    double timeUntilEdge = std::numeric_limits<double>::infinity();
};

struct ClockSample
{
    double time;
    bool high;
    bool available = true;
};

class Circuit;

using SignalSample = ClockSample;

struct SignalTrace
{
    ClockPath path;
    PinType direction;
    unsigned int pinIndex;
    std::string pinId, label;
    std::deque<SignalSample> samples;
    double shortestInterval = 0;
};

/** @brief Session-only, bounded edge history. Never serialized or included in editor snapshots. */
class ClockTimeline
{
  public:
    static constexpr std::size_t sampleLimit = 2048;

    /** @brief Append clock transitions while retaining bounded history and the latest observation time. */
    void record(double time, const std::vector<ClockReading>& readings, bool edge = false);

    void clear();
    static constexpr std::size_t signalLimit = 8;

    /** @brief Toggle a pin; false means the eight-probe limit was reached. */
    bool toggleSignal(SignalTrace trace);

    bool tracesSignal(const SignalTrace& trace) const;

    void removeSignal(std::size_t index);

    /** @brief Remap selected signal paths after topology changes so stale runtime identities are not observed. */
    bool prepareSignals(std::uint64_t revision);

    /** @brief Record level or availability transitions for valid selected pins and discard vanished signal paths. */
    void recordSignals(const Circuit& circuit, bool available = true);

    std::vector<SignalTrace>& signals() { return m_signals; }
    const std::vector<SignalTrace>& signals() const { return m_signals; }
    double time() const { return m_time; }
    const std::map<ClockPath, std::deque<ClockSample>>& traces() const { return m_traces; }
    const std::deque<double>& steps() const { return m_steps; }

  private:
    std::vector<SignalTrace> m_signals;
    std::optional<std::uint64_t> m_signalRevision;
    double m_time = 0;
    std::map<ClockPath, std::deque<ClockSample>> m_traces;
    std::deque<double> m_steps;
};
