#pragma once
#include <cstddef>
#include <deque>
#include <map>
#include <vector>

using ClockPath = std::vector<int>;

/** Circuit-local IDs identify a native clock or a clock inside nested live instances. */
struct ClockReading
{
    ClockPath path;
    float frequency;
    bool paused, high;
};

struct ClockSample
{
    double time;
    bool high;
};

/** Session-only, bounded edge history. Never serialized or included in editor snapshots. */
class ClockTimeline
{
  public:
    static constexpr std::size_t sampleLimit = 2048;
    void record(double time, const std::vector<ClockReading>& readings, bool edge = false);
    void clear();

    double time() const { return m_time; }

    const std::map<ClockPath, std::deque<ClockSample>>& traces() const { return m_traces; }

    const std::deque<double>& steps() const { return m_steps; }

  private:
    double m_time = 0;
    std::map<ClockPath, std::deque<ClockSample>> m_traces;
    std::deque<double> m_steps;
};
