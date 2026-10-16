#pragma once
#include "Diagnostics/EditProfile.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <tuple>
#include <vector>

class Scene;

namespace bench
{
using Clock = std::chrono::steady_clock;

inline double milliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

struct Config
{
    std::vector<int> sizes{100, 500, 1000, 2000, 4096};
    int trials = 3, samples = 40, warmup = 8;
    int editSamples = 0, previewBatch = 1000;
    double enduranceSeconds = 30;
    std::string suite = "all";
    std::filesystem::path output = "out/benchmarks/latest";
    std::vector<std::string> argv;
    bool requireRendering = false;
    bool editProfile = LOGIC_BENCHMARK_EDIT_PROFILE != 0;
    double lifecycleSeconds = 30;
    int coverageSamples = 3;
};

class Recorder
{
  public:
    explicit Recorder(const Config& config);
    void sample(
        const std::string& scenario,
        int size,
        int trial,
        int index,
        const std::string& metric,
        double value,
        const std::string& unit
    );
    void outcome(
        const std::string& scenario,
        int size,
        const std::string& status,
        const std::string& detail = ""
    );
    void finish(bool success);
    nlohmann::json metadata;

  private:
    std::filesystem::path m_directory;
    std::ofstream m_samples;
};

void check(bool condition, const std::string& message);
void memorySample(Recorder& results, const std::string& scenario, int size, int index);
void rendering(const Config& config, Recorder& results);
Scene editorFixture(int size);
void checkConnection(const Scene& scene, int source, int sink);
void clockCorrectness(Recorder& results);
void validatorChecks(Recorder& results);
void profileChecks(Recorder& results);
void extendedCoverage(const Config& config, Recorder& results);
void lifecycle(const Config& config, Recorder& results);
void recordProfile(
    Recorder& results,
    const std::string& scenario,
    int size,
    int trial,
    int sample,
    const edit_profile::Capture& capture,
    double totalMs
);

template <class F>
auto timedEdit(bool enabled, F&& operation)
{
    edit_profile::Capture capture;
    edit_profile::Session session(enabled ? &capture : nullptr);
    const auto start = Clock::now();
    auto value = operation();
    const double elapsed = milliseconds(start);
    return std::tuple{std::move(value), elapsed, capture};
}
} // namespace bench
