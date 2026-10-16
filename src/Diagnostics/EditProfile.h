#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <utility>

// Compiled away in ordinary builds. No editor decisions depend on these probes.
#ifndef LOGIC_BENCHMARK_EDIT_PROFILE
#define LOGIC_BENCHMARK_EDIT_PROFILE 0
#endif
namespace edit_profile
{
enum class Phase
{
    SceneCopy,
    Snapshot,
    Routing,
    Normalization,
    Connectivity,
    Restore,
    Count
};

inline constexpr std::array names{"scene_copy", "snapshot", "routing", "normalization", "connectivity", "restore"};

struct Value
{
    double inclusiveMs = 0, exclusiveMs = 0;
    std::uint64_t calls = 0;
};

struct Capture
{
    std::array<Value, static_cast<unsigned>(Phase::Count)> values{};
};

#if LOGIC_BENCHMARK_EDIT_PROFILE
class Scope;
inline thread_local Capture* active = nullptr;
inline thread_local Scope* parent = nullptr;

class Session
{
  public:
    explicit Session(Capture* capture) : previous(active) { active = capture; }
    ~Session() { active = previous; }
    Session(const Session&) = delete;

  private:
    Capture* previous;
};

class Scope
{
  public:
    explicit Scope(Phase value) : capture(active), phase(value)
    {
        if (capture)
        {
            outer = parent;
            parent = this;
            start = Clock::now();
        }
    }

    /** @brief Accumulate inclusive and exclusive phase time, then charge the elapsed interval to the enclosing scope.
     */
    ~Scope()
    {
        if (!capture)
            return;
        const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        auto& value = capture->values[static_cast<unsigned>(phase)];
        value.inclusiveMs += elapsed;
        value.exclusiveMs += elapsed - children;
        ++value.calls;
        parent = outer;
        if (outer)
            outer->children += elapsed;
    }
    Scope(const Scope&) = delete;

  private:
    using Clock = std::chrono::steady_clock;
    Capture* capture;
    Phase phase;
    Scope* outer = nullptr;
    Clock::time_point start;
    double children = 0;
};
#else

struct Session
{
    explicit Session(Capture*) {}
};

struct Scope
{
    explicit Scope(Phase) {}
};
#endif

template <class F>
decltype(auto) measure(Phase phase, F&& operation)
{
    Scope scope(phase);
    return std::forward<F>(operation)();
}
} // namespace edit_profile
