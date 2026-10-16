#include "Benchmark.h"
#include "Components/Definitions/NativeDefinitions.h"
#include "Editor/Actions/EditHistory.h"
#include "Editor/Actions/EditorActions.h"
#include "Editor/ExampleCircuits.h"
#include "Editor/Scene.h"
#include "Editor/Subcircuits.h"
#include "Geometry/GridMetrics.h"
#include "Geometry/WireRouting.h"
#include "Persistence/CircuitFile.h"
#include "Simulation/Subcircuit.h"
#include "Validation.h"

#include <algorithm>
#include <map>
#include <numeric>

namespace bench
{
Scene editorFixture(int size)
{
    Scene scene;
    EditBatch batch;
    for (int i = 0; i < size / 2; ++i)
    {
        batch.push_back(CreateComponent{BuiltinComponentIds::Input, {0, i * 6}, {}, PlacementPolicy::AllowOverlap});
        batch.push_back(CreateComponent{BuiltinComponentIds::Output, {12, i * 6}, {}, PlacementPolicy::AllowOverlap});
        batch.push_back(AddWire{{{1, i * 6}, {11, i * 6}}});
    }
    const auto result = EditorActions(scene).apply(batch);
    check(bool(result), result.message);
    return scene;
}


void checkConnection(const Scene& scene, int source, int sink)
{
    check(
        scene.netOfPin({source, 0}, PinType::OUTPUT) == scene.netOfPin({sink, 0}, PinType::INPUT) &&
            scene.netOfPin({source, 0}, PinType::OUTPUT) != INVALID_NET_ID && scene.getShortedNetCount() == 0,
        "Structural benchmark disconnected or shorted its circuit."
    );
}


namespace
{
using Json = nlohmann::json;


/** @brief Capture logical pins, directed edges and runtime diagnostics for full-scene equivalence checks. */
Json runtimeImage(const Scene& scene)
{
    Json image = Json::array();
    std::vector<int> ids;
    for (const auto& [id, view] : scene.getComponentViewMap())
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    std::map<int, int> ordinal;
    for (std::size_t i = 0; i < ids.size(); ++i)
        ordinal[ids[i]] = int(i);
    for (const int id : ids)
    {
        const auto* component = scene.getLogicComponent(id);
        Json pins = Json::array(), edges = Json::array();
        for (int pin = 0; pin < component->getInputPinCount(); ++pin)
            pins.push_back(component->getStateInPin(pin));
        for (int pin = 0; pin < component->getOutputPinCount(); ++pin)
            pins.push_back(component->getStateOutPin(pin));
        for (const auto& edge : component->getOutConnections())
            edges.push_back({edge.srcPinIndex, ordinal.at(edge.destComponentId), edge.destPinIndex});
        std::sort(edges.begin(), edges.end());
        image.push_back({{"pins", pins}, {"edges", edges}});
    }
    return image;
}


/** @brief Compare complete persisted designs and their recovered logical behaviour after propagation. */
void equivalent(const Scene& original, const Scene& recovered, const std::string& name)
{
    check(
        Json::parse(circuitToJson(original, name)) == Json::parse(circuitToJson(recovered, name)),
        "Recovered circuit differs in persisted design fields."
    );
    check(
        runtimeImage(original) == runtimeImage(recovered) && original.netCount() == recovered.netCount() &&
            original.getShortedNetCount() == recovered.getShortedNetCount(),
        "Recovered connectivity/runtime differs."
    );
}


/** @brief Measure warmed application save/load costs and verify complete design and logical roundtrips. */
void persistence(const Config& config, Recorder& results)
{
    for (const int size : config.sizes)
    {
        auto scene = editorFixture(size);
        for (int id = 0; id < size; id += 6)
            scene.handleClick(id);
        check(scene.propagate() == SimulationResult::OK, "Persistence fixture did not settle.");
        const auto path = config.output / ("persistence-" + std::to_string(size) + ".json");
        for (int trial = 0; trial < config.trials; ++trial)
            for (int sample = -1; sample < config.coverageSamples; ++sample)
            {
                auto start = Clock::now();
                saveCircuit(path, scene, "roundtrip");
                const double saveMs = milliseconds(start);
                start = Clock::now();
                auto recovered = loadCircuit(path);
                const double loadMs = milliseconds(start);
                check(recovered.name == "roundtrip", "Circuit name changed on disk.");
                check(recovered.scene.propagate() == SimulationResult::OK, "Loaded fixture did not settle.");
                equivalent(scene, recovered.scene, "roundtrip");
                if (sample >= 0)
                {
                    results.sample("save_circuit", size, trial, sample, "duration_ms", saveMs, "ms");
                    results.sample("load_circuit", size, trial, sample, "duration_ms", loadMs, "ms");
                    results.sample(
                        "save_circuit", size, trial, sample, "file_bytes", double(std::filesystem::file_size(path)),
                        "bytes"
                    );
                }
            }
        results.outcome(
            "persistence_roundtrip", size, "pass",
            "Actual atomic save/load; all persisted JSON fields and logical pins/edges compared; "
            "warmed OS cache, not stable-media latency. Transient clock/latch state is outside the "
            "file format."
        );
    }
}


/** @brief Route a fixture connection through normal geometry validation and reject invalid setup. */
void connect(Scene& scene, int source, int target)
{
    const auto* from = scene.getCommittedComponentView(source);
    const auto* to = scene.getCommittedComponentView(target);
    const auto a = from->getAbsolutePinGridPos(from->getOutputPins().at(0));
    const auto b = to->getAbsolutePinGridPos(to->getInputPins().at(0));
    const auto path =
        routeWire(a, b, scene.committedGeometry(), scene.getWires(), scene.netOfPin({source, 0}, PinType::OUTPUT));
    check(bool(path), "Nested fixture route failed.");
    const auto result = EditorActions(scene).apply({AddWire{*path}});
    check(bool(result), result.message);
}


/** @brief Build a reusable inverter nested to the requested depth with stable external interface identities. */
Scene nestedFixture(int depth)
{
    Scene leaf;
    leaf.addComponent(BuiltinComponentIds::Input, {-20, 0});
    leaf.addComponent(BuiltinComponentIds::Not, {0, 0});
    leaf.addComponent(BuiltinComponentIds::Output, {20, 0});
    connect(leaf, 0, 1);
    connect(leaf, 1, 2);
    check(leaf.setInterfaceNamingRequired(true), "Cannot name nested interface.");
    auto definition = makeSubcircuit(leaf, {"subcircuit.benchmark.nested.0", 1}, "Nested inverter");
    for (int level = 1; level <= depth; ++level)
    {
        Scene wrapper;
        auto registered = EditorActions(wrapper).apply({RegisterComponentDefinition{definition}});
        check(bool(registered), registered.message);
        wrapper.addComponent(BuiltinComponentIds::Input, {-20, 0});
        wrapper.addComponent(definition.identity.id, {0, 0});
        wrapper.addComponent(BuiltinComponentIds::Output, {20, 0});
        connect(wrapper, 0, 1);
        connect(wrapper, 1, 2);
        if (level == depth)
            return wrapper;
        check(wrapper.setInterfaceNamingRequired(true), "Cannot name nested wrapper.");
        definition =
            makeSubcircuit(wrapper, {"subcircuit.benchmark.nested." + std::to_string(level), 1}, "Nested inverter");
    }
    throw std::runtime_error("Invalid nesting depth.");
}


/** @brief Measure nested propagation and verify each nested output after every input transition. */
void nested(const Config& config, Recorder& results)
{
    for (const int depth : {1, 2, 4})
    {
        auto scene = nestedFixture(depth);
        check(scene.propagate() == SimulationResult::OK, "Nested fixture failed initial settling.");
        auto recovered = circuitFromJson(circuitToJson(scene, "nested"));
        check(recovered.scene.propagate() == SimulationResult::OK, "Nested roundtrip did not settle.");
        equivalent(scene, recovered.scene, "nested");
        for (int trial = 0; trial < config.trials; ++trial)
            for (int sample = -config.warmup; sample < config.samples; ++sample)
            {
                scene.handleClick(0);
                const bool input = scene.getLogicComponent(0)->getStateOutPin(0);
                const auto start = Clock::now();
                const auto status = scene.propagate();
                const double elapsed = milliseconds(start);
                check(
                    status == SimulationResult::OK && scene.getLogicComponent(2)->getStateInPin(0) != input,
                    "Nested propagation output mismatch."
                );
                const Component* current = scene.getLogicComponent(1);
                int seen = 0;
                while (const auto* sub = dynamic_cast<const Subcircuit*>(current))
                {
                    check(sub->getStateOutPin(0) != input, "Nested internal output mismatch.");
                    ++seen;
                    current = sub->runtimeCircuit().getComponent(1);
                }
                check(seen == depth, "Benchmark did not exercise the intended nesting depth.");
                if (sample >= 0)
                    results.sample("nested_propagation", depth, trial, sample, "duration_ms", elapsed, "ms");
            }
        results.outcome(
            "nested_propagation", depth, "pass",
            "Nested inverter, full design roundtrip, each nested output verified after every input "
            "change; size means nesting depth."
        );
    }
}


int named(const Scene& scene, const std::string& label)
{
    for (const auto& [id, view] : scene.getComponentViewMap())
        if (view->getBodyLabel() == label)
            return id;
    throw std::runtime_error("Counter fixture label missing: " + label);
}


/** @brief Measure a real two-bit sequential counter while verifying every state, timestamp and recorded output
 * transition. */
void sequential(const Config& config, Recorder& results)
{
    auto scene = buildExampleCircuit(CircuitExample::Counter);
    const int low = named(scene, "Bit 0 (1)"), high = named(scene, "Bit 1 (2)");
    for (int trial = 0; trial < config.trials; ++trial)
    {
        scene.resetSimulation();
        ClockTimeline timeline;
        check(
            timeline.toggleSignal(
                {{low}, PinType::INPUT, 0, scene.getCommittedComponentView(low)->getInputPins().at(0).id, "bit0", {}}
            ) &&
                timeline.toggleSignal(
                    {{high},
                     PinType::INPUT,
                     0,
                     scene.getCommittedComponentView(high)->getInputPins().at(0).id,
                     "bit1",
                     {}}
                ),
            "Cannot trace counter outputs."
        );
        std::array<std::deque<ClockSample>, 2> expected{{{{0, false}}, {{0, false}}}};
        for (int edge = 1; edge <= 256; ++edge)
        {
            const auto start = Clock::now();
            const int processed = scene.stepAllClocks(1, &timeline);
            const double elapsed = milliseconds(start);
            const int value = (edge + 1) / 2 % 4;
            check(
                processed == 1 && scene.getLastEvalResult() == SimulationResult::OK &&
                    scene.getLogicComponent(low)->getStateInPin(0) == bool(value & 1) &&
                    scene.getLogicComponent(high)->getStateInPin(0) == bool(value & 2) &&
                    std::abs(scene.clockTime() - edge * .5) < 1e-10,
                "Real sequential counter state/time mismatch."
            );
            for (int bit = 0; bit < 2; ++bit)
            {
                const bool state = value & (1 << bit);
                if (expected[bit].back().high != state)
                    expected[bit].push_back({edge * .5, state});
            }
            results.sample(
                "sequential_counter_edge", int(scene.getComponentCount()), trial, edge - 1, "duration_ms", elapsed, "ms"
            );
        }
        check(timeline.signals().size() == 2, "Sequential counter probes were removed.");
        for (int bit = 0; bit < 2; ++bit)
            check(
                traceMatches(timeline.signals()[bit].samples, expected[bit]), "Sequential counter probe trace mismatch."
            );
    }
    results.outcome(
        "sequential_counter_edge", int(scene.getComponentCount()), "pass",
        "Real two-bit synchronous counter with D flip-flops and XOR feedback; 256 half cycles per "
        "trial; every state/time and complete bit traces verified. Includes Scene synchronization, "
        "no GUI pacing."
    );
}


/** @brief Measure obstacle detours and branched-source movement while independently checking clearance and sink
 * connectivity. */
void routing(const Config& config, Recorder& results)
{
    for (const int count : {1, 8, 32})
    {
        std::vector<ComponentGeometry> obstacles;
        for (int i = 0; i < count; ++i)
            obstacles.push_back({i, {i * 8, 0}, i * 8 * GridMetrics::Spacing, 0, .2f, .2f, {}});
        const GridCoords start{-8, 0}, end{count * 8, 0};
        for (int trial = 0; trial < config.trials; ++trial)
            for (int sample = -config.warmup; sample < config.samples; ++sample)
            {
                auto [path, elapsed, capture] =
                    timedEdit(config.editProfile, [&] { return routeWire(start, end, obstacles, {}); });
                check(path && path->front() == start && path->back() == end, "Obstacle routing failed.");
                for (std::size_t segment = 1; segment < path->size(); ++segment)
                {
                    const auto a = (*path)[segment - 1], b = (*path)[segment];
                    check(a.x == b.x || a.y == b.y, "Routing produced diagonal geometry.");
                    for (const auto& obstacle : obstacles)
                    {
                        // Independent geometric oracle for these exact .2-wide square obstacles.
                        const double left = obstacle.origin.x - 2.5, right = obstacle.origin.x + 2.5;
                        const bool intersects =
                            a.y == b.y
                                ? std::abs(a.y) < 2.5 && std::max(a.x, b.x) > left && std::min(a.x, b.x) < right
                                : a.x > left && a.x < right && std::max(a.y, b.y) > -2.5 && std::min(a.y, b.y) < 2.5;
                        check(!intersects, "Route violates obstacle clearance.");
                    }
                }
                if (sample >= 0)
                {
                    results.sample("route_obstacles", count, trial, sample, "duration_ms", elapsed, "ms");
                    if (config.editProfile)
                        recordProfile(results, "route_obstacles", count, trial, sample, capture, elapsed);
                }
            }
        results.outcome(
            "route_obstacles", count, "pass",
            "Independent segment/clearance oracle; bounded production search unchanged; size means "
            "obstacle count."
        );
    }
    Scene junction;
    const auto created = EditorActions(junction).apply(
        {CreateComponent{BuiltinComponentIds::Input, {-12, 0}, {.inputState = true}},
         CreateComponent{BuiltinComponentIds::Output, {12, 0}}, CreateComponent{BuiltinComponentIds::Output, {0, 8}},
         AddWire{{{-11, 0}, {11, 0}}}, AddWire{{{-6, 0}, {-6, 8}, {-1, 8}}}}
    );
    check(bool(created), created.message);
    EditHistory history;
    for (int trial = 0; trial < config.trials; ++trial)
        for (int sample = -1; sample < config.coverageSamples; ++sample)
        {
            auto [moved, elapsed, capture] = timedEdit(
                config.editProfile, [&] { return EditorActions(junction).apply({MoveComponent{0, {-14, 0}}}); }
            );
            check(bool(moved), moved.message);
            history.record(moved);
            check(junction.propagate() == SimulationResult::OK, "Junction circuit did not settle.");
            checkConnection(junction, 0, 1);
            checkConnection(junction, 0, 2);
            check(!junction.getWireIntersections().empty(), "Move erased a branch junction.");
            check(
                junction.getLogicComponent(1)->getStateInPin(0) && junction.getLogicComponent(2)->getStateInPin(0),
                "Junction move lost a driven sink."
            );
            check(bool(history.undo(junction)), "Cannot restore junction move.");
            history.clear();
            if (sample >= 0)
            {
                results.sample("move_junction", 3, trial, sample, "duration_ms", elapsed, "ms");
                if (config.editProfile)
                    recordProfile(results, "move_junction", 3, trial, sample, capture, elapsed);
            }
        }
    results.outcome(
        "move_junction", 3, "pass",
        "Rerouted moved source while preserving both driven sinks and a fixed branch junction."
    );
}


/** @brief Capture persisted design, runtime and revision state that rejected edits must leave unchanged. */
Json atomicImage(const Scene& scene)
{
    Json wires = Json::array();
    for (const auto& [id, wire] : scene.getWires())
        wires.push_back({id, wire.getNet(), int(wire.getState())});
    return {
        {"design", Json::parse(circuitToJson(scene, "atomic"))},
        {"runtime", runtimeImage(scene)},
        {"revision", scene.getRevision()},
        {"builds", scene.getTopologyBuildCount()},
        {"wires", wires},
        {"net_count", scene.netCount()},
        {"result", int(scene.getLastEvalResult())},
        {"dirty", scene.isSimulationDirty()},
        {"clock_time", scene.clockTime()},
        {"pending", scene.pendingClockTime()}
    };
}


/** @brief Measure expected early and late edit rejections and verify complete scene rollback. */
void rejected(const Config& config, Recorder& results)
{
    auto scene = editorFixture(16);
    check(scene.propagate() == SimulationResult::OK, "Atomic fixture did not settle.");
    for (const bool routingFailure : {false, true})
    {
        Scene target = scene;
        if (routingFailure)
        {
            target = Scene{};
            target.addComponent(BuiltinComponentIds::Input, {-10, 0});
            target.addComponent(BuiltinComponentIds::Output, {10, 0});
            auto wire = EditorActions(target).apply({AddWire{{{-9, 0}, {9, 0}}}});
            check(bool(wire), wire.message);
            target.addComponent(BuiltinComponentIds::Output, {3, 0});
            check(target.propagate() == SimulationResult::OK, "Rejected-route fixture did not settle.");
        }
        const auto original = atomicImage(target);
        const EditBatch batch = routingFailure
                                    ? EditBatch{MoveComponent{0, {0, 0}}}
                                    : EditBatch{
                                          ConfigureComponentProperties{.componentId = 0, .label = "must rollback"},
                                          MoveComponent{0, {12, 0}}
                                      };
        const std::string scenario = routingFailure ? "reject_routing_atomic" : "reject_batch_atomic";
        for (int trial = 0; trial < config.trials; ++trial)
            for (int sample = -config.warmup; sample < config.samples; ++sample)
            {
                auto [result, elapsed, capture] =
                    timedEdit(config.editProfile, [&] { return EditorActions(target).apply(batch); });
                check(
                    !result && !result.change && result.createdComponentIds.empty() && result.insertedWireIds.empty(),
                    "Expected rejection produced a change."
                );
                check(atomicImage(target) == original, "Rejected operation partially changed the scene.");
                if (routingFailure)
                    check(result.error == EditError::InvalidWire, "Routing rejection had an unexpected cause.");
                if (sample >= 0)
                {
                    results.sample(
                        scenario, int(target.getComponentCount()), trial, sample, "rejected_operation_ms", elapsed, "ms"
                    );
                    if (config.editProfile)
                        recordProfile(
                            results, scenario, int(target.getComponentCount()), trial, sample, capture, elapsed
                        );
                }
            }
        results.outcome(
            scenario, int(target.getComponentCount()), "pass",
            "Expected typed rejection; complete persisted design, pin states/edges, wire "
            "IDs/nets/states, revision, build count, clock state and result unchanged. Rejected "
            "timings are separate."
        );
    }
}


/** @brief Build driven comb networks whose degree-three branches retain the exact requested route count. */
Scene denseFixture(int wireCount)
{
    // Driven comb nets: degree-three branch points prevent normalization from
    // merging the requested route density away. Equal numbers of adjacent odd
    // route counts make the exact even persistence-limit counts possible.
    const int rows = wireCount < 1024 ? 8 : 128;
    check(wireCount % rows == 0 && (wireCount / rows) % 2 == 0, "Invalid comb route count.");
    EditBatch batch;
    for (int row = 0; row < rows; ++row)
    {
        const int count = wireCount / rows + (row < rows / 2 ? -1 : 1);
        const int branches = (count - 1) / 2, y = row * 8;
        batch.push_back(
            CreateComponent{BuiltinComponentIds::Input, {-2, y}, {.inputState = true}, PlacementPolicy::AllowOverlap}
        );
        batch.push_back(
            CreateComponent{BuiltinComponentIds::Output, {4 * branches + 1, y}, {}, PlacementPolicy::AllowOverlap}
        );
        batch.push_back(AddWire{{{-1, y}, {0, y}}});
        for (int branch = 0; branch < branches; ++branch)
        {
            const int x = branch * 4;
            batch.push_back(AddWire{{{x, y}, {x, y + 3}}});
            batch.push_back(AddWire{{{x, y}, {x + 4, y}}});
        }
    }
    Scene scene;
    const auto result = EditorActions(scene).apply(batch);
    check(bool(result), result.message);
    check(scene.wireCount() == std::size_t(wireCount), "Normalization changed dense route count.");
    check(
        scene.propagate() == SimulationResult::OK && scene.getShortedNetCount() == 0, "Dense fixture did not settle."
    );
    scene.syncVisuals();
    for (const auto& [id, wire] : scene.getWires())
        check(wire.getState() == PinState::ON, "Dense route is not driven.");
    return scene;
}


/** @brief Measure rebuild and persistence at increasing route density, verifying exact counts and complete recovered
 * designs. */
void density(const Config& config, Recorder& results)
{
    const auto counts =
        config.sizes == std::vector<int>{16} ? std::vector<int>{64} : std::vector<int>{1024, 4096, 8192, 16384};
    for (const int wires : counts)
    {
        auto scene = denseFixture(wires);
        const auto canonical = Json::parse(circuitToJson(scene, "dense"));
        for (int trial = 0; trial < config.trials; ++trial)
        {
            auto [ok, elapsed, capture] = timedEdit(
                config.editProfile,
                [&]
                {
                    scene.rebuildNets();
                    return true;
                }
            );
            check(
                ok && scene.propagate() == SimulationResult::OK && scene.wireCount() == std::size_t(wires),
                "Dense rebuild failed."
            );
            check(Json::parse(circuitToJson(scene, "dense")) == canonical, "Dense rebuild changed authored geometry.");
            for (int sink = 1; sink < int(scene.getComponentCount()); sink += 2)
                check(scene.getLogicComponent(sink)->getStateInPin(0), "Dense sink disconnected.");
            results.sample("dense_rebuild", wires, trial, 0, "duration_ms", elapsed, "ms");
            results.sample(
                "dense_rebuild", wires, trial, 0, "component_count", double(scene.getComponentCount()), "count"
            );
            results.sample("dense_rebuild", wires, trial, 0, "route_count", double(scene.wireCount()), "count");
            if (config.editProfile)
                recordProfile(results, "dense_rebuild", wires, trial, 0, capture, elapsed);
        }
        const auto path = config.output / ("dense-" + std::to_string(wires) + ".json");
        const auto saveStart = Clock::now();
        saveCircuit(path, scene, "dense");
        results.sample("dense_save", wires, 0, 0, "duration_ms", milliseconds(saveStart), "ms");
        const auto loadStart = Clock::now();
        auto loaded = loadCircuit(path);
        results.sample("dense_load", wires, 0, 0, "duration_ms", milliseconds(loadStart), "ms");
        check(loaded.scene.propagate() == SimulationResult::OK, "Dense recovered fixture failed.");
        equivalent(scene, loaded.scene, "dense");
        results.outcome(
            "dense_rebuild", wires, "pass",
            "Exact driven comb route count retained through rebuild and disk roundtrip; size means "
            "routes; one measured rebuild per trial, save/load once per process; no rare-tail "
            "claim."
        );
    }
}
} // namespace


void extendedCoverage(const Config& config, Recorder& results)
{
    // Deliberately incorrect recovered circuits must fail the actual validator.
    auto original = editorFixture(16);
    check(original.propagate() == SimulationResult::OK, "Roundtrip control fixture failed.");
    for (int corruption = 0; corruption < 3; ++corruption)
    {
        Scene bad(original);
        EditBatch batch;
        if (corruption == 0)
            batch.push_back(ConfigureComponentProperties{.componentId = 0, .label = "corrupt"});
        else if (corruption == 1)
            batch.push_back(ConfigureInput{0, true});
        else
            batch.push_back(AddWire{{{100, 100}, {110, 100}}});
        const auto changed = EditorActions(bad).apply(batch);
        check(bool(changed), changed.message);
        check(bad.propagate() == SimulationResult::OK, "Corrupt control did not settle.");
        bool rejected = false;
        try
        {
            equivalent(original, bad, "control");
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        check(
            rejected && atomicImage(original) != atomicImage(bad),
            "Roundtrip/atomic validator accepted a corrupted scene."
        );
    }
    results.outcome(
        "scene_negative_controls", 16, "pass",
        "Actual design/runtime validators reject a changed label, input state and extra route."
    );
    persistence(config, results);
    nested(config, results);
    sequential(config, results);
    routing(config, results);
    rejected(config, results);
    density(config, results);
}


void lifecycle(const Config& config, Recorder& results)
{
    constexpr int size = 128;
    int cycles = 0;
    auto cycle = [&]
    {
        auto scene = editorFixture(size);
        const auto original = Json::parse(circuitToJson(scene, "cycle"));
        Scene copy(scene);
        EditHistory history;
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            auto moved = EditorActions(copy).apply({MoveComponent{size - 1, {14, (size / 2 - 1) * 6}}});
            check(bool(moved), moved.message);
            history.record(moved);
            check(
                bool(history.undo(copy)) && bool(history.redo(copy)) && bool(history.undo(copy)),
                "Lifecycle undo/redo failed."
            );
            checkConnection(copy, size - 2, size - 1);
        }
        check(Json::parse(circuitToJson(copy, "cycle")) == original, "Lifecycle failed to restore complete design.");
        check(history.undoCount() <= EditHistory::Limit, "Lifecycle history overflowed.");
        ++cycles;
        // All scenes, snapshots, catalogs and histories die before the memory checkpoint.
    };
    for (int i = 0; i < 20; ++i)
        cycle();
    cycles = 0;
    memorySample(results, "scene_lifecycle_endurance", size, 0);
    const auto start = Clock::now();
    int seconds = 0, lastCycles = 0;
    while (milliseconds(start) < config.lifecycleSeconds * 1000)
    {
        cycle();
        if (milliseconds(start) >= (seconds + 1) * 1000)
        {
            ++seconds;
            memorySample(results, "scene_lifecycle_endurance", size, seconds);
            results.sample(
                "scene_lifecycle_endurance", size, 0, seconds, "elapsed_seconds", milliseconds(start) / 1000, "s"
            );
            results.sample(
                "scene_lifecycle_endurance", size, 0, seconds, "verified_cycles", cycles - lastCycles, "count"
            );
            lastCycles = cycles;
        }
    }
    memorySample(results, "scene_lifecycle_endurance", size, seconds + 1);
    results.sample("scene_lifecycle_endurance", size, 0, 0, "measured_seconds", milliseconds(start) / 1000, "s");
    results.sample("scene_lifecycle_endurance", size, 0, 0, "verified_total_cycles", cycles, "count");
    results.outcome(
        "scene_lifecycle_endurance", size, "pass",
        "Repeated create/copy/destroy scenes and histories; three move/undo/redo/undo cycles per "
        "scene, complete restored-design comparison. Memory after destruction, warmed allocator; "
        "bounded observation, not proof of leak freedom."
    );
}
} // namespace bench
