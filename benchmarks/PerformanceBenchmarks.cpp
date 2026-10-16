#include "Benchmark.h"
#include "BuildManifest.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditHistory.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"
#include "Persistence/CircuitFile.h"
#include "Sha256.h"
#include "Simulation/Circuit.h"
#include "Validation.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#else
#include <unistd.h>
#endif

namespace bench
{
/** @brief Format system time as a UTC timestamp for run and manifest provenance. */
std::string utcTime()
{
    const auto now = std::chrono::system_clock::now();
    const auto raw = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}


void check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}


Recorder::Recorder(const Config& config) : m_directory(config.output)
{
    std::filesystem::create_directories(m_directory.parent_path().empty() ? "." : m_directory.parent_path());
    check(
        std::filesystem::create_directory(m_directory),
        "Output directory already exists; choose a new --output directory."
    );
    m_samples.open(m_directory / "samples.csv");
    check(bool(m_samples), "Cannot write benchmark samples.");
    m_samples << "scenario,size,trial,sample,metric,value,unit\n" << std::setprecision(12);
    metadata = {
        {"build_type", BENCHMARK_BUILD_TYPE},
        {"pointer_bits", sizeof(void*) * 8},
        {"hardware_threads", std::thread::hardware_concurrency()},
        {"suite", config.suite},
        {"sizes", config.sizes},
        {"trials", config.trials},
        {"samples_per_trial", config.samples},
        {"warmup_operations", config.warmup},
        {"endurance_seconds", config.enduranceSeconds},
        {"lifecycle_seconds", config.lifecycleSeconds},
        {"coverage_samples_per_trial", config.coverageSamples},
        {"edit_profile_compiled", bool(LOGIC_BENCHMARK_EDIT_PROFILE)},
        {"edit_profile_enabled", config.editProfile},
        {"arguments", config.argv},
        {"started_utc", utcTime()},
        {"run_id", m_directory.filename().string()},
        {"repetition_scope", "trials share this process; independent executions have distinct run_id/process_id"},
        {"edit_samples_override", config.editSamples},
        {"preview_operations_per_batch", config.previewBatch},
        {"steady_clock_period_seconds", double(Clock::period::num) / Clock::period::den},
        {"cases", nlohmann::json::array()},
        {"notes",
         {"External steady-clock timings; existing production instrumentation remains enabled.",
          "Fixture construction, warmup, CSV output and correctness checks are outside operation "
          "timings.",
          "Headless throughput is not whole-app FPS or displayed input latency.",
          "Power mode, CPU clocks and background processes are not controlled by this runner."}}
    };
    std::string payload;
    for (const auto* chunk : benchmarkManifestChunks)
        payload += chunk;
    check(sha256({payload.begin(), payload.end()}) == benchmarkManifestHash, "Embedded manifest checksum mismatch.");
    const auto manifest = nlohmann::json::parse(payload);
    metadata["manifest_sha256"] = benchmarkManifestHash;
    metadata["revision"] = manifest["revision"];
    metadata["git_status_at_build"] = manifest["git_status"];
    metadata["build_captured_utc"] = manifest["captured_utc"];
    metadata["build_tools"] = {
        {"compiler", manifest["compiler"]},
        {"compiler_version", manifest["compiler_version"]},
        {"cmake_version", manifest["cmake_version"]},
        {"python_version", manifest["python_version"]}
    };
    std::ofstream(m_directory / "build_manifest.json", std::ios::binary) << payload;
    std::filesystem::path executable = std::filesystem::absolute(config.argv.front());
#ifdef _WIN32
    std::vector<wchar_t> module(32768);
    const auto length = GetModuleFileNameW(nullptr, module.data(), DWORD(module.size()));
    check(length > 0 && length < module.size(), "Cannot resolve running executable.");
    executable = std::filesystem::path(std::wstring(module.data(), length));
    metadata["process_id"] = GetCurrentProcessId();
#else
    if (std::filesystem::exists("/proc/self/exe"))
        executable = std::filesystem::read_symlink("/proc/self/exe");
    metadata["process_id"] = getpid();
#endif
    metadata["executable"] = executable.string();
    metadata["executable_sha256"] = fileHash(executable);
    metadata["working_directory"] = std::filesystem::current_path().string();
    metadata["workspace_changes_since_build"] = nlohmann::json::array();
    bool shaderChanged = false;
    for (const auto& [name, hash] : manifest["files"].items())
    {
        const auto path = std::filesystem::path(manifest["source_root"].get<std::string>()) / name;
        if (!std::filesystem::exists(path) || fileHash(path) != hash.get<std::string>())
        {
            metadata["workspace_changes_since_build"].push_back(name);
            shaderChanged |= name.starts_with("assets/shaders/");
        }
    }
    // Shaders are loaded at runtime; mismatching them would misidentify the measured renderer.
    metadata["runtime_shader_hashes_match_build"] = !shaderChanged;
    bool resourceChanged = false;
    for (const auto& [name, hash] : manifest["runtime_resources"].items())
        if (!std::filesystem::exists(name) || fileHash(name) != hash.get<std::string>())
            resourceChanged = true;
    metadata["runtime_resource_hashes_match_build"] = !resourceChanged;
    metadata["build_tools"]["other"] = manifest["tools"];
#ifdef _MSC_VER
    metadata["compiler"] = "MSVC " + std::to_string(_MSC_FULL_VER);
#else
    metadata["compiler"] = __VERSION__;
#endif
#ifdef _WIN32
    metadata["os"] = "Windows";
    char name[256]{};
    DWORD bytes = sizeof(name);
    if (RegGetValueA(
            HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString",
            RRF_RT_REG_SZ, nullptr, name, &bytes
        ) == ERROR_SUCCESS)
        metadata["cpu"] = name;
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory))
        metadata["physical_ram_bytes"] = memory.ullTotalPhys;
#else
    metadata["os"] = "non-Windows";
#endif
    finish(false); // An interrupted run retains metadata and its completed raw samples.
    check(
        (!shaderChanged && !resourceChanged) || (config.suite != "render" && config.suite != "all"),
        "Runtime shaders/font changed after build; rebuild before measuring rendering."
    );
}


void Recorder::sample(
    const std::string& scenario, int size, int trial, int index, const std::string& metric, double value,
    const std::string& unit
)
{
    check(std::isfinite(value), "Non-finite benchmark result.");
    m_samples << scenario << ',' << size << ',' << trial << ',' << index << ',' << metric << ',' << value << ',' << unit
              << '\n';
    check(bool(m_samples), "Failed writing benchmark samples.");
}


void Recorder::outcome(const std::string& scenario, int size, const std::string& status, const std::string& detail)
{
    metadata["cases"].push_back({{"scenario", scenario}, {"size", size}, {"status", status}, {"detail", detail}});
    m_samples.flush();
    finish(false);
    std::cout << scenario << " size=" << size << ": " << status;
    if (!detail.empty())
        std::cout << " (" << detail << ')';
    std::cout << std::endl;
}


void Recorder::finish(bool success)
{
    metadata["run_completed"] = success;
    metadata["updated_utc"] = utcTime();
    if (success)
        metadata["finished_utc"] = utcTime();
    std::ofstream file(m_directory / "metadata.json");
    check(bool(file), "Cannot write benchmark metadata.");
    file << metadata.dump(2) << '\n';
    m_samples.flush();
}


void memorySample(Recorder& results, const std::string& scenario, int size, int index)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    check(
        GetProcessMemoryInfo(
            GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)
        ),
        "Cannot read process memory."
    );
    results.sample(scenario, size, 0, index, "private_bytes", double(counters.PrivateUsage), "bytes");
    results.sample(scenario, size, 0, index, "working_set_bytes", double(counters.WorkingSetSize), "bytes");
    DWORD handles = 0;
    check(GetProcessHandleCount(GetCurrentProcess(), &handles), "Cannot read process handle count.");
    results.sample(scenario, size, 0, index, "process_handles", handles, "count");
#else
    (void)results;
    (void)scenario;
    (void)size;
    (void)index;
#endif
}


namespace
{
void connection(Circuit& circuit, int source, int output, int target, int input)
{
    check(circuit.connectComponents(source, output, target, input), "Invalid benchmark connection.");
}


struct SettleAttempt
{
    SimulationResult status;
    SimulationWork work;
    std::size_t remaining;
    double elapsed;
};


SettleAttempt settle(Circuit& circuit)
{
    circuit.takeSimulationWork();
    std::size_t remaining = 100000; // Identical to Circuit::propagate()'s production budget.
    const auto start = Clock::now();
    const auto status = circuit.propagate(remaining);
    const double elapsed = milliseconds(start);
    return {status, circuit.takeSimulationWork(), remaining, elapsed};
}


/** @brief Record settling work and classify completed versus aborted propagation under the unchanged production limits.
 */
void recordAttempt(
    Recorder& results, const std::string& scenario, int size, int trial, int sample, const std::string& phase,
    const SettleAttempt& attempt
)
{
    const bool ok = attempt.status == SimulationResult::OK;
    const auto prefix = phase + (ok ? "_completed" : "_aborted");
    results.sample(scenario, size, trial, sample, prefix + "_ms", attempt.elapsed, "ms");
    results.sample(scenario, size, trial, sample, phase + "_evaluations", double(attempt.work.evaluations), "count");
    results.sample(scenario, size, trial, sample, phase + "_rounds", double(attempt.work.rounds), "count");
    results.metadata["propagation_attempts"].push_back(
        {{"scenario", scenario},
         {"size", size},
         {"trial", trial},
         {"sample", sample},
         {"phase", phase},
         {"reason", propagationReason(attempt.status, attempt.remaining, attempt.work)},
         {"evaluations", attempt.work.evaluations},
         {"rounds", attempt.work.rounds},
         {"remaining_evaluations", attempt.remaining}}
    );
}


/** @brief Build the requested fan-out or NOT-chain topology and establish its initial input state. */
Circuit logicFixture(int size, bool chain)
{
    Circuit circuit;
    const int source = circuit.addInputPin();
    int previous = source;
    for (int i = 1; i < size; ++i)
    {
        const int gate = circuit.addGate(NOT);
        connection(circuit, chain ? previous : source, 0, gate, 0);
        previous = gate;
    }
    return circuit;
}


/** @brief Measure cold settling and input changes separately, then locate the cold-chain evaluation boundary. */
void propagation(const Config& config, Recorder& results)
{
    for (const bool chain : {false, true})
        for (const int size : config.sizes)
        {
            const std::string scenario = chain ? "propagation_chain" : "propagation_fanout";
            auto circuit = logicFixture(size, chain);
            const auto initial = settle(circuit);
            recordAttempt(results, scenario, size, 0, 0, "cold", initial);
            if (initial.status != SimulationResult::OK)
            {
                results.outcome(
                    scenario, size, "limit", propagationReason(initial.status, initial.remaining, initial.work)
                );
                continue;
            }
            bool limited = false;
            std::string reason;
            for (int trial = 0; trial < config.trials && !limited; ++trial)
                for (int sample = -config.warmup; sample < config.samples; ++sample)
                {
                    auto* input = static_cast<InputPin*>(circuit.getComponent(0));
                    input->toggle();
                    circuit.markStateDirty();
                    const bool state = input->getStateOutPin();
                    const auto attempt = settle(circuit);
                    if (attempt.status != SimulationResult::OK)
                    {
                        recordAttempt(results, scenario, size, trial, sample, "change", attempt);
                        limited = true;
                        reason = propagationReason(attempt.status, attempt.remaining, attempt.work);
                        break;
                    }
                    for (int i = 1; i < size; ++i)
                        check(
                            circuit.getComponent(i)->getStateOutPin() == (chain ? state != bool(i % 2) : !state),
                            "Incorrect gate output in propagation benchmark."
                        );
                    if (sample >= 0)
                    {
                        recordAttempt(results, scenario, size, trial, sample, "change", attempt);
                        results.sample(scenario, size, trial, sample, "duration_ms", attempt.elapsed, "ms");
                    }
                }
            results.outcome(
                scenario, size, limited ? "limit" : "pass",
                limited ? reason : "Every gate output checked after every operation."
            );
        }
    // Independently establish the bracket, then search integer component counts.
    // This boundary is for initially-low input + cold NOT chain, not all topologies.
    auto probe = [&](int size)
    {
        auto circuit = logicFixture(size, true);
        const auto attempt = settle(circuit);
        recordAttempt(results, "chain_cold_boundary", size, 0, 0, "cold", attempt);
        return attempt.status == SimulationResult::OK;
    };
    int low = 10, high = 4096;
    check(probe(low), "Boundary search lacks a passing lower endpoint.");
    check(!probe(high), "Boundary search lacks a failing upper endpoint.");
    while (high - low > 1)
    {
        const int middle = low + (high - low) / 2;
        if (probe(middle))
            low = middle;
        else
            high = middle;
    }
    results.metadata["chain_cold_boundary"] = {
        {"largest_settled", low},
        {"first_failed", high},
        {"assumption", "Monotone cold all-NOT chain, initially-low input, default limits; integer sizes include "
                       "odd counts."}
    };
    results.outcome(
        "chain_cold_boundary", high, "pass",
        "Adjacent passing/failing sizes identified without increasing production limits."
    );
}


class EdgeProbe : public Component
{
  public:
    explicit EdgeProbe(Circuit& owner) : Component(-1, 1, 0), circuit(&owner) {}


    std::unique_ptr<Component> clone() const override { return std::make_unique<EdgeProbe>(*this); }


    void evaluate() override {}


    int clockInputPin() const override { return 0; }


    void onClockEdge(bool high) override
    {
        ordered &= high == (edges % 2 == 0);
        ++edges;
        events.push_back({circuit->clockTime(), high});
    }


    unsigned edges = 0;
    bool ordered = true;
    Circuit* circuit;
    std::deque<ClockSample> events;
};


/** @brief Measure drained clock execution with and without probes, checking complete traces and untimed per-edge
 * replays. */
void clocks(const Config& config, Recorder& results)
{
    for (const int size : config.sizes)
        for (const int frequency : {1, 64, 256, 1024, 8192})
            for (const bool tracing : {false, true})
            {
                const std::string scenario =
                    "clock_" + std::to_string(frequency) + "hz" + (tracing ? "_8probes" : "_no_probes");
                for (int trial = 0; trial < config.trials; ++trial)
                {
                    Circuit circuit;
                    const int clock = circuit.addClock(float(frequency));
                    const int probe = circuit.addComponent(std::make_unique<EdgeProbe>(circuit));
                    connection(circuit, clock, 0, probe, 0);
                    ClockTimeline timeline;
                    std::vector<int> gates;
                    for (int i = 2; i < size; ++i)
                    {
                        const int gate = circuit.addGate(NOT);
                        connection(circuit, clock, 0, gate, 0);
                        gates.push_back(gate);
                        if (tracing && timeline.signals().size() < 8)
                            check(
                                timeline.toggleSignal({{gate}, PinType::OUTPUT, 0, "", "probe", {}}),
                                "Cannot add benchmark probe."
                            );
                    }
                    check(circuit.propagate() == SimulationResult::OK, "Clock fixture did not settle.");
                    auto* counter = static_cast<EdgeProbe*>(circuit.getComponent(probe));
                    // Warm the identical circuit, phase, probe containers and propagation path.
                    const auto warming = Clock::now();
                    circuit.updateClocks(float(16.0 / frequency), tracing ? &timeline : nullptr);
                    while (circuit.pendingClockTime() > 0)
                    {
                        check(milliseconds(warming) < 10000, "Clock warmup drain watchdog expired.");
                        circuit.updateClocks(0, tracing ? &timeline : nullptr);
                    }
                    check(milliseconds(warming) < 10000, "Clock warmup drain watchdog expired.");
                    counter->edges = 0;
                    counter->ordered = true;
                    counter->events.clear();
                    const double before = circuit.clockTime();
                    const float horizon = float(128.0 / frequency); // Exactly 256 edges; binary-exact duration.
                    const auto start = Clock::now();
                    int calls = 0;
                    std::vector<double> durations;
                    do
                    {
                        const auto call = Clock::now();
                        circuit.updateClocks(calls == 0 ? horizon : 0, tracing ? &timeline : nullptr);
                        durations.push_back(milliseconds(call));
                        ++calls;
                        check(milliseconds(start) < 10000, "Clock benchmark exceeded its 10-second drain timeout.");
                    } while (circuit.pendingClockTime() > 0);
                    const double elapsed = milliseconds(start);
                    check(
                        counter->edges == 256 && counter->ordered &&
                            std::abs(circuit.clockTime() - before - horizon) < 1e-10 &&
                            !circuit.getComponent(clock)->getStateOutPin(),
                        "Clock benchmark lost time, phase or edges."
                    );
                    for (const int gate : gates)
                        check(circuit.getComponent(gate)->getStateOutPin(), "Clock fan-out ended at the wrong state.");
                    std::deque<ClockSample> expectedEdges;
                    for (int edge = 1; edge <= 256; ++edge)
                        expectedEdges.push_back({before + double(edge) / (2 * frequency), bool(edge % 2)});
                    check(
                        traceMatches(counter->events, expectedEdges),
                        "Clock throughput lost, duplicated or mistimed an edge."
                    );
                    if (tracing)
                    {
                        std::deque<ClockSample> expectedClocks{{0, false}}, expectedSignals{{0, true}};
                        for (int edge = 1; edge <= 288; ++edge)
                        {
                            const double time = double(edge) / (2 * frequency);
                            expectedClocks.push_back({time, bool(edge % 2)});
                            expectedSignals.push_back({time, !bool(edge % 2)});
                        }
                        check(
                            timeline.traces().size() == 1 &&
                                traceMatches(timeline.traces().at({clock}), expectedClocks),
                            "Clock throughput trace mismatch."
                        );
                        check(timeline.signals().size() == 8, "Clock throughput lost probes.");
                        for (const auto& signal : timeline.signals())
                            check(
                                traceMatches(signal.samples, expectedSignals) &&
                                    std::abs(signal.shortestInterval - 1.0 / (2 * frequency)) < 1e-10,
                                "Clock throughput probe trace mismatch."
                            );
                        check(timeline.steps().size() == 288, "Clock throughput step count mismatch.");
                        for (int edge = 1; edge <= 288; ++edge)
                            check(
                                std::abs(timeline.steps()[edge - 1] - double(edge) / (2 * frequency)) < 1e-10,
                                "Clock throughput step timestamp mismatch."
                            );
                    }
                    for (std::size_t i = 0; i < durations.size(); ++i)
                        results.sample(scenario, size, trial, int(i), "update_call_ms", durations[i], "ms");
                    results.sample(scenario, size, trial, 0, "batch_ms", elapsed, "ms");
                    results.sample(
                        scenario, size, trial, 0, "sim_seconds_per_wall_second", horizon * 1000 / elapsed, "ratio"
                    );
                    results.sample(scenario, size, trial, 0, "verified_edges", counter->edges, "count");
                    // Replay the same phase and topology outside measurements, stopping at
                    // every edge to inspect all settled outputs (bulk calls can drain many edges).
                    const auto replay = Clock::now();
                    for (int edge = 1; edge <= 256; ++edge)
                    {
                        circuit.updateClocks(float(1.0 / (2 * frequency)));
                        while (circuit.pendingClockTime() > 0)
                        {
                            check(milliseconds(replay) < 10000, "Per-edge replay watchdog expired.");
                            circuit.updateClocks(0);
                        }
                        check(
                            milliseconds(replay) < 10000 && circuit.getLastEvalResult() == SimulationResult::OK,
                            "Per-edge replay failed or timed out."
                        );
                        for (const int gate : gates)
                            check(
                                circuit.getComponent(gate)->getStateOutPin() == !bool(edge % 2),
                                "Incorrect intermediate fanout state."
                            );
                    }
                }
                results.outcome(
                    scenario, size, "pass",
                    "256 timestamped edges per trial; complete traces and untimed per-edge replay; "
                    "headless capacity, not a "
                    "paced GUI test."
                );
            }
}


/** @brief Measure committed moves, undo/redo and preview batches, including phase capture and paired collection
 * overhead. */
void editor(const Config& config, Recorder& results)
{
    for (const int size : config.sizes)
    {
        auto scene = editorFixture(size);
        const int sink = size / 2 * 2 - 1, source = sink - 1;
        const auto original = scene.getCommittedComponentView(sink)->getGridPosition();
        EditHistory history;
        // Expensive edits use fewer samples; raw counts remain explicit in the report.
        const int samples =
            config.editSamples > 0 ? config.editSamples : std::min(config.samples, size >= 1000 ? 5 : 12);
        for (int trial = 0; trial < config.trials; ++trial)
            for (int sample = -1; sample < samples; ++sample)
            {
                history.clear();
                const auto builds = scene.getTopologyBuildCount();
                auto [moved, moveMs, moveProfile] = timedEdit(
                    config.editProfile,
                    [&] { return EditorActions(scene).apply({MoveComponent{sink, {original.x + 2, original.y}}}); }
                );
                check(bool(moved), moved.message);
                history.record(moved);
                checkConnection(scene, source, sink);
                check(scene.getTopologyBuildCount() == builds + 1, "An edit rebuilt topology more than once.");
                auto [undone, undoMs, undoProfile] = timedEdit(config.editProfile, [&] { return history.undo(scene); });
                check(
                    bool(undone) && scene.getCommittedComponentView(sink)->getGridPosition() == original,
                    "Undo benchmark failed."
                );
                checkConnection(scene, source, sink);
                auto [redone, redoMs, redoProfile] = timedEdit(config.editProfile, [&] { return history.redo(scene); });
                check(bool(redone), redone.message);
                checkConnection(scene, source, sink);
                check(bool(history.undo(scene)), "Cannot reset structural benchmark.");
                if (sample >= 0)
                {
                    results.sample("move_connected", size, trial, sample, "duration_ms", moveMs, "ms");
                    if (config.editProfile)
                    {
                        recordProfile(results, "move_connected", size, trial, sample, moveProfile, moveMs);
                        recordProfile(results, "undo_connected_move", size, trial, sample, undoProfile, undoMs);
                        recordProfile(results, "redo_connected_move", size, trial, sample, redoProfile, redoMs);
                    }
                    results.sample("undo_connected_move", size, trial, sample, "duration_ms", undoMs, "ms");
                    results.sample("redo_connected_move", size, trial, sample, "duration_ms", redoMs, "ms");
                }
            }
        results.outcome(
            "move_connected", size, "pass",
            "Includes snapshots, routing, normalization and connectivity; excludes GUI "
            "input/rendering."
        );
        const auto builds = scene.getTopologyBuildCount();
        const auto preview = EditorActions(scene).beginMove(sink);
        check(bool(preview), "Cannot start move preview.");
        for (int trial = 0; trial < config.trials; ++trial)
            for (int sample = -config.warmup; sample < config.samples; ++sample)
            {
                const auto start = Clock::now();
                bool success = true;
                for (int operation = 0; operation < config.previewBatch; ++operation)
                    success &= EditorActions(scene).previewMove(*preview, {original.x + 2 + operation % 2, original.y});
                const double elapsed = milliseconds(start);
                check(success && scene.getTopologyBuildCount() == builds, "Preview mutated committed topology.");
                if (sample >= 0)
                    results.sample(
                        "move_preview", size, trial, sample, "mean_operation_ms", elapsed / config.previewBatch, "ms"
                    );
            }
        check(EditorActions(scene).cancelMove(*preview), "Cannot cancel move preview.");
        results.outcome("move_preview", size, "pass");
        if (config.editProfile)
        {
            // Paired enabled/disabled collection in the same compiled executable.
            // Alternate order across trials to expose drift instead of assuming a
            // universal subtraction. Reset and verification stay outside timing.
            for (int trial = 0; trial < config.trials; ++trial)
                for (const bool enabled : std::array{trial % 2 == 0, trial % 2 != 0})
                {
                    auto [moved, elapsed, capture] = timedEdit(
                        enabled,
                        [&] { return EditorActions(scene).apply({MoveComponent{sink, {original.x + 2, original.y}}}); }
                    );
                    check(bool(moved), moved.message);
                    checkConnection(scene, source, sink);
                    history.record(moved);
                    check(bool(history.undo(scene)), "Cannot reset instrumentation pair.");
                    history.clear();
                    results.sample(
                        enabled ? "move_collection_on" : "move_collection_off", size, trial, 0, "duration_ms", elapsed,
                        "ms"
                    );
                    if (enabled)
                        recordProfile(results, "move_collection_on", size, trial, 0, capture, elapsed);
                }
            results.outcome(
                "move_collection_pair", size, "pass",
                "Same fixture/binary, paired collection on/off, order alternated across trials. "
                "Small paired count measures overhead/noise, not rare tails; disabled probes still "
                "have a branch in this binary."
            );
        }
        memorySample(results, "editor_memory", size, 0);
    }
}


/** @brief Observe warmed bounded-history memory while verifying repeated connected edits and the history retention
 * limit. */
void endurance(const Config& config, Recorder& results)
{
    constexpr int size = 128;
    auto scene = editorFixture(size);
    EditHistory history;
    int operations = 0;
    auto step = [&]
    {
        const auto result =
            EditorActions(scene).apply({MoveComponent{size - 1, {operations % 2 ? 12 : 14, (size / 2 - 1) * 6}}});
        check(bool(result), result.message);
        history.record(result);
        ++operations;
        check(history.undoCount() <= EditHistory::Limit, "History exceeded its retention limit.");
        checkConnection(scene, size - 2, size - 1);
    };
    for (int i = 0; i < 220; ++i)
        step(); // Reach the bounded history plateau before taking memory samples.
    memorySample(results, "history_endurance", size, 0);
    const auto start = Clock::now();
    int seconds = 0;
    while (milliseconds(start) < config.enduranceSeconds * 1000)
    {
        step();
        if (milliseconds(start) >= (seconds + 1) * 1000)
        {
            ++seconds;
            memorySample(results, "history_endurance", size, seconds);
            results.sample("history_endurance", size, 0, seconds, "undo_records", double(history.undoCount()), "count");
        }
    }
    memorySample(results, "history_endurance", size, seconds + 1);
    results.sample("history_endurance", size, 0, 0, "measured_seconds", milliseconds(start) / 1000, "s");
    results.sample("history_endurance", size, 0, 0, "verified_edits", operations - 220, "count");
    results.outcome(
        "history_endurance", size, "pass",
        "Short warmed memory observation, not proof of leak freedom; history retained up to 100 "
        "records."
    );
}


/** @brief Parse and validate workload controls, preserving the full invocation for the run's provenance record. */
Config arguments(int argc, char** argv)
{
    Config config;
    config.argv.assign(argv, argv + argc);
    config.output = std::filesystem::path("out/benchmarks") /
                    ("run-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
                     std::to_string(std::random_device{}()));
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        if (key == "--require-rendering")
        {
            config.requireRendering = true;
            continue;
        }
        if (key == "--smoke")
        {
            config.sizes = {16};
            config.trials = 1;
            config.samples = 3;
            config.warmup = 1;
            config.enduranceSeconds = 0.05;
            config.lifecycleSeconds = 0.05;
            config.coverageSamples = 1;
            continue;
        }
        check(i + 1 < argc, "Missing value for " + key);
        const std::string value = argv[++i];
        if (key == "--output")
            config.output = value;
        else if (key == "--suite")
            config.suite = value;
        else if (key == "--trials")
            config.trials = std::stoi(value);
        else if (key == "--samples")
            config.samples = std::stoi(value);
        else if (key == "--edit-samples")
            config.editSamples = std::stoi(value);
        else if (key == "--preview-batch")
            config.previewBatch = std::stoi(value);
        else if (key == "--warmup")
            config.warmup = std::stoi(value);
        else if (key == "--edit-profile")
        {
            check(value == "on" || value == "off", "--edit-profile must be on or off.");
            config.editProfile = value == "on";
            check(
                !config.editProfile || LOGIC_BENCHMARK_EDIT_PROFILE,
                "Edit profiling is not compiled into this executable."
            );
        }
        else if (key == "--coverage-samples")
            config.coverageSamples = std::stoi(value);
        else if (key == "--lifecycle-seconds")
            config.lifecycleSeconds = std::stod(value);
        else if (key == "--endurance-seconds")
            config.enduranceSeconds = std::stod(value);
        else if (key == "--sizes")
        {
            config.sizes.clear();
            std::istringstream values(value);
            std::string part;
            while (std::getline(values, part, ','))
                config.sizes.push_back(std::stoi(part));
        }
        else
            throw std::runtime_error("Unknown argument: " + key);
    }
    check(
        !config.sizes.empty() && config.trials > 0 && config.samples > 0 && config.warmup >= 0 &&
            std::isfinite(config.enduranceSeconds) && config.enduranceSeconds >= 0 && config.editSamples >= 0 &&
            config.previewBatch > 0 && std::isfinite(config.lifecycleSeconds) && config.lifecycleSeconds >= 0 &&
            config.coverageSamples > 0,
        "Invalid benchmark arguments."
    );
    for (const int size : config.sizes)
        check(size >= 10 && size <= 4096 && size % 2 == 0, "Sizes must be even values from 10 through 4096.");
    check(
        config.suite == "all" || config.suite == "cpu" || config.suite == "simulation" || config.suite == "clocks" ||
            config.suite == "editor" || config.suite == "endurance" || config.suite == "render" ||
            config.suite == "validators" || config.suite == "coverage" || config.suite == "lifecycle" ||
            config.suite == "memory",
        "Unknown suite; use all, cpu, simulation, clocks, editor, endurance, render, validators, "
        "coverage, lifecycle or memory."
    );
    return config;
}
} // namespace
} // namespace bench


/** @brief Run the requested benchmark suites and retain explicit incomplete metadata when validation or execution
 * fails. */
int main(int argc, char** argv)
{
    std::unique_ptr<bench::Recorder> results;
    try
    {
        const auto config = bench::arguments(argc, argv);
        results = std::make_unique<bench::Recorder>(config);
        const auto selected = [&](const char* suite)
        {
            return config.suite == "all" || config.suite == suite ||
                   (config.suite == "cpu" && std::string(suite) != "render") ||
                   (config.suite == "memory" &&
                    (std::string(suite) == "endurance" || std::string(suite) == "lifecycle"));
        };
        bench::validatorChecks(*results);
        bench::profileChecks(*results);
        if (selected("clocks") || config.suite == "validators")
            bench::clockCorrectness(*results);
        if (selected("simulation"))
            bench::propagation(config, *results);
        if (selected("clocks"))
            bench::clocks(config, *results);
        if (selected("editor"))
            bench::editor(config, *results);
        if (selected("endurance"))
            bench::endurance(config, *results);
        if (selected("coverage"))
            bench::extendedCoverage(config, *results);
        if (selected("lifecycle"))
            bench::lifecycle(config, *results);
#if BENCHMARK_RENDERING
        if (selected("render"))
            bench::rendering(config, *results);
#else
        if (selected("render"))
        {
            results->outcome("rendering", 0, "unavailable", "Built without the OpenGL application.");
            bench::check(
                !config.requireRendering, "Rendering is required (--require-rendering), but this executable was built "
                                          "without OpenGL (BUILD_SIMULATOR_APP=OFF)."
            );
        }
#endif
        results->finish(true);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "BENCHMARK FAILED: " << error.what() << '\n';
        if (results)
        {
            results->metadata["error"] = error.what();
            results->finish(false);
        }
        return 1;
    }
}
