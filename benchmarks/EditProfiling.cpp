#include "Benchmark.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditHistory.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/Scene.h"

#include <numeric>

namespace bench
{
void recordProfile(
    Recorder& results, const std::string& scenario, int size, int trial, int sample,
    const edit_profile::Capture& capture, double totalMs
)
{
    double exclusive = 0;
    for (std::size_t i = 0; i < capture.values.size(); ++i)
    {
        const auto& value = capture.values[i];
        check(
            value.exclusiveMs >= -1e-6 && value.inclusiveMs + 1e-6 >= value.exclusiveMs,
            "Invalid nested profile accounting."
        );
        const auto prefix = std::string("phase_") + edit_profile::names[i];
        results.sample(scenario, size, trial, sample, prefix + "_inclusive_ms", value.inclusiveMs, "ms");
        results.sample(scenario, size, trial, sample, prefix + "_exclusive_ms", value.exclusiveMs, "ms");
        results.sample(scenario, size, trial, sample, prefix + "_calls", double(value.calls), "count");
        exclusive += value.exclusiveMs;
    }
    check(exclusive <= totalMs + 1e-4, "Edit phase accounting exceeds global time.");
    results.sample(scenario, size, trial, sample, "unattributed_ms", totalMs - exclusive, "ms");
}


void profileChecks(Recorder& results)
{
    Scene scene;
    auto fixture = EditorActions(scene).apply(
        {CreateComponent{BuiltinComponentIds::Input, {0, 0}}, CreateComponent{BuiltinComponentIds::Output, {12, 0}},
         AddWire{{{1, 0}, {11, 0}}}}
    );
    check(bool(fixture), fixture.message);
    auto [moved, total, capture] =
        timedEdit(true, [&] { return EditorActions(scene).apply({MoveComponent{1, {14, 0}}}); });
    check(bool(moved), moved.message);
#if LOGIC_BENCHMARK_EDIT_PROFILE
    const auto calls = [&](edit_profile::Phase phase) { return capture.values[static_cast<unsigned>(phase)].calls; };
    check(
        calls(edit_profile::Phase::Snapshot) == 2 && calls(edit_profile::Phase::SceneCopy) == 1 &&
            calls(edit_profile::Phase::Normalization) == 1 && calls(edit_profile::Phase::Connectivity) == 1 &&
            calls(edit_profile::Phase::Routing) > 0,
        "Edit phase probes missed real operations."
    );
    recordProfile(results, "profile_accounting_control", 2, 0, 0, capture, total);
    EditHistory history;
    history.record(moved);
    auto [undone, undoMs, undoCapture] = timedEdit(true, [&] { return history.undo(scene); });
    check(
        bool(undone) && undoCapture.values[static_cast<unsigned>(edit_profile::Phase::Restore)].calls == 1,
        "Undo restore probe missing."
    );
    recordProfile(results, "profile_accounting_control_undo", 2, 0, 0, undoCapture, undoMs);
    auto [off, offMs, offCapture] =
        timedEdit(false, [&] { return EditorActions(scene).apply({MoveComponent{1, {14, 0}}}); });
    check(bool(off), off.message);
    for (const auto& value : offCapture.values)
        check(value.calls == 0, "Disabled collection still records phases.");
    // Nested scopes and exception unwinding restore the active collector/parent.
    edit_profile::Capture unwind;
    {
        edit_profile::Session session(&unwind);
        try
        {
            edit_profile::Scope outer(edit_profile::Phase::Restore);
            edit_profile::Scope inner(edit_profile::Phase::SceneCopy);
            throw 1;
        }
        catch (int)
        {
        }
        edit_profile::Scope next(edit_profile::Phase::Snapshot);
    }
    check(
        unwind.values[0].calls == 1 && unwind.values[1].calls == 1 && unwind.values[5].calls == 1,
        "Profile unwinding corrupted nesting."
    );
    for (int enabled = 0; enabled < 2; ++enabled)
    {
        constexpr int operations = 20000;
        edit_profile::Capture timer;
        edit_profile::Session session(enabled ? &timer : nullptr);
        const auto start = Clock::now();
        for (int i = 0; i < operations; ++i)
        {
            edit_profile::Scope scope(edit_profile::Phase::Snapshot);
        }
        results.sample(
            "profile_probe_calibration", 0, 0, enabled, enabled ? "enabled_mean_scope_ms" : "disabled_mean_scope_ms",
            milliseconds(start) / operations, "ms"
        );
    }
#else
    for (const auto& value : capture.values)
        check(value.calls == 0, "Compile-disabled probes produced samples.");
#endif
    results.outcome(
        "edit_profile_controls", 2, "pass",
        "Real phase call counts, exclusive accounting, disabled collection and exception "
        "unwinding; probes compile away when benchmark profiling is disabled."
    );
}
} // namespace bench
