# Release subsystem benchmarks

Opt-in measurements of the existing simulator, editor, history and OpenGL renderer.
Algorithms, the 100000-evaluation budget and the 1024-round limit remain unchanged.
These are subsystem measurements on the recorded machine, not application FPS,
displayed input latency or a universal speed score.

## Build and run

Use a Visual Studio Developer PowerShell in the repository root. Python 3 is
required only when building the optional benchmark target; plotting is optional.

```powershell
cmake --preset release -DBUILD_BENCHMARKS=ON
cmake --build --preset release --target PerformanceBenchmarks --parallel 4
& .\out\build\release\PerformanceBenchmarks.exe --smoke --suite cpu
ctest --preset release --output-on-failure
python benchmarks/campaign.py out/build/release/PerformanceBenchmarks.exe --profile dev --runs 2
```

`--output` must name a NEW directory, including when an existing directory is
empty. Each executable invocation has its own raw CSV, metadata and immutable
build snapshot. Interruptions retain completed samples and `run_completed=false`.
No previous result is removed or replaced. Default directories have a timestamp
and random suffix. Run benchmark processes sequentially on quiet hardware.

Campaign profiles:

- `dev`: 16/100/500 components, one within-process trial, five samples, one edit
  sample, two warmups, one-second history observation; two independent processes
  by default. This is a development check, not a well-powered tail-latency study.
- `extended`: all five original sizes, ten trials, 200 samples, 40 edit samples,
  16 warmups, 600-second endurance per process. Large editing cases make this
  intentionally expensive. Choose `--runs` and the process watchdog explicitly.

Override profile values after `--`, for example:

```powershell
python benchmarks/campaign.py out/build/release/PerformanceBenchmarks.exe --runs 3 --timeout-seconds 1800 --output out/benchmarks/my-campaign -- --sizes 100,500,1000,2000,4096 --trials 3 --samples 40 --edit-samples 5 --endurance-seconds 30
```

Executable options: `--suite all|cpu|simulation|clocks|editor|endurance|render|validators|coverage|lifecycle|memory`,
`--sizes 100,500,1000,2000,4096` (even 10..4096), `--trials 3`, `--samples 40`,
`--warmup 8`, `--endurance-seconds 30`, `--edit-samples N`, `--preview-batch 1000`,
`--output NEW_DIRECTORY`, `--smoke`, `--require-rendering`, `--edit-profile on|off`,
`--coverage-samples 3` and `--lifecycle-seconds 30`. `memory` selects only the two
memory workloads; `coverage` selects the additional scenarios below. Without an explicit
edit count, the short configuration caps edits at 12 below 1000 components and
five otherwise. Zero warmups are allowed; normal measurement campaigns should use
warmups. The clock workload always warms 32 edges to start at an identical phase.
`--require-rendering` makes unavailable OpenGL a failure, as used in the rendering
validator CTest. Ordinary runs record unavailable rendering explicitly. A build with
`BUILD_SIMULATOR_APP=OFF` rejects required rendering for both `render` and `all`,
returns nonzero and retains `run_completed=false`. Its dedicated CTest exercises
all four required/optional combinations against that actual headless executable:

```powershell
cmake -S . -B out/build/release-headless -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SIMULATOR_APP=OFF -DBUILD_BENCHMARKS=ON -DBUILD_TESTING=ON
cmake --build out/build/release-headless --target PerformanceBenchmarks --parallel 4
ctest --test-dir out/build/release-headless -R benchmark_headless_rendering_contract --output-on-failure
```

The runner provides a `validation` campaign profile: three within-process trials,
200 normal samples per trial, 40 edits per trial (120 per size/process), eight
warmups, five save/load and junction samples per trial, and 120 seconds per memory
workload. Choose at least three independent processes explicitly with `--runs 3`.
This is substantially longer than development mode, including several-minute
editing and route-limit cases; it is still not proof about rare pauses.

```powershell
python benchmarks/campaign.py out/build/release/PerformanceBenchmarks.exe --profile validation --runs 3 --suite editor --timeout-seconds 3600 --output out/benchmarks/validation-edits
python benchmarks/campaign.py out/build/release/PerformanceBenchmarks.exe --profile validation --runs 3 --suite coverage --timeout-seconds 3600 --output out/benchmarks/validation-coverage
python benchmarks/campaign.py out/build/release/PerformanceBenchmarks.exe --profile validation --runs 3 --suite memory --timeout-seconds 1200 --output out/benchmarks/validation-memory
```

## Correctness and timing boundaries

**Propagation:** one input and N-1 NOT gates, arranged as fan-out or a linear
chain. Every gate is checked after each successful input change. Cold settling
and input changes have separate metrics. `*_completed_ms` and `*_aborted_ms`
never enter the same summary; `duration_ms` contains successful input changes
only. Each recorded attempt stores evaluation count, rounds, remaining budget
and one of `settled`, `evaluation_budget_exhausted`, `round_limit_reached`, or
`component_failure`. Existing `takeSimulationWork()` counters and the existing
shared-budget overload provide this diagnosis for these flat fixtures; production
code does not need extra instrumentation. Actual settled, exhausted-budget and
oscillating round-limit circuits test the classifier. `propagation_attempts` in
metadata and raw count metrics preserve the evidence.

An integer binary search brackets the cold NOT-chain boundary between passing
10 and failing 4096 components, then identifies adjacent passing/failing sizes.
The search assumes monotonicity for this specific cold topology and low initial
input. Its samples are separate from the normal cases. It is not a universal
component limit; the same-size fan-out may settle successfully.

**Clocks:** one clock, an independent timestamped edge observer and N-2 NOT gates;
1/64/256/1024/8192 Hz, zero/eight probes. Warmup drains 32 edges; each measured batch
drains 256 edges. Every observer timestamp/value and the complete clock, probe and
step histories are compared to mathematical expectations. Each trial also replays
256 edges outside timing, checking all gate outputs after each settled edge.
Observer recording is inside clock calls, while comparisons, CSV and the replay
are outside their timing. `batch_ms` includes the drain loop and observer work;
`update_call_ms` measures individual production calls. Watchdogs cover warmup,
measured draining and replay. `sim_seconds_per_wall_second` is headless drained
capacity; it does not include the application's frame budget, rendering or input.

Separate untimed multi-clock oracles use 64/128/128 Hz, both edge-by-edge and in a
large drained batch. They test simultaneous edges, every intermediate output in
the edge-by-edge run, all observer events, complete retained traces and the
2048-sample retention limit, shortest intervals, compression and availability.
Deliberate missing, duplicated, reordered, mistimed, wrong-valued and unavailable
samples must be rejected by the same trace validator.

**Editing/history:** N/2 independent input/output pairs with one route each.
Move the last output two cells; check connectivity, shorts and the topology build
count; undo/redo and reset the fixture. Full scene snapshots, routing,
normalization and connectivity are inside operation timings; checks and fixture
reset are outside. The current rise in editing cost is an observation, not an
attribution to a particular phase. No optimizer is introduced by these benchmarks.

**Edit phases:** `BENCHMARK_EDIT_PROFILING=ON` compiles probes only when
`BUILD_BENCHMARKS=ON`; ordinary builds have empty scopes optimized away. A benchmark
can disable collection with `--edit-profile off`. Snapshot and candidate-scene
copies surround the full constructors, including member initialization; routing
probes surround endpoint rerouting, fitting and individual route searches;
normalization and connectivity have separate scopes. Undo/redo also expose their
whole restore scope. These nested scopes record inclusive/exclusive time and call
counts. Only exclusive phases plus `unattributed_ms` add to the global operation
time. Inclusive restore includes the nested copies/normalization/connectivity;
its exclusive remainder includes its own propagation, synchronization and other
restore work. Copy/snapshot phases classify the transactional editor call sites,
not every possible Scene copy performed elsewhere in the application.

`duration_ms` retains the original whole-operation timing. Calibration reports the
batch mean cost of an empty enabled/disabled scope (not an individual-call tail).
Each edit size additionally pairs one move with collection on and off per trial,
alternating order across trials and restoring/verifying outside timing. These
pairs reveal overhead and measurement noise rather than assuming a fixed universal
subtraction. Disabled collection still executes probe branches in that executable;
use `-DBENCHMARK_EDIT_PROFILING=OFF` and a separate build to remove them altogether.
Phase bookkeeping after an inner stop timestamp contributes to its parent's
exclusive time or to the residual. Counts and nested accounting are checked on
real moves/undo, disabled collection and exception unwinding.

**Move preview:** each timed sample batches `--preview-batch` alternating updates;
`mean_operation_ms` divides elapsed batch time by the operation count. The result
includes loop/call overhead. Its percentiles describe batch means, not individual
operation tails. Correctness and unchanged committed topology are checked after
the batch; warmup batches are discarded.

**History endurance:** 128 components; warm with 220 moves to reach the bounded
100-record history plateau. Sample whole-process private memory, working set and
handle counts while continuing verified edits. Allocator caches and earlier suites
also affect whole-process memory. A short flat graph does not establish leak freedom.

**Rendering:** fixed 1920x1080 RGBA8 offscreen framebuffer, N bodies, 3N pins/leads,
N routes, no labels/UI/events/swap/VSync/simulation. The zoom fits the scene: visible
symbols get smaller as N increases. Uniform vs mixed shaders changes composition
and batching, so that comparison cannot isolate batching cost. The controlled
`render_mixed_*` / `render_mixed_grouped_*` pair keeps AND/OR/XOR composition,
positions, shaders, pins and routes identical. Only stable body IDs determining
batch ordering change. Bodies do not overlap. Full RGBA framebuffer equality is
checked outside timing for each grouped trial against the interleaved ordering.

- `cpu_submit_ms`: begin-frame and canvas submission only.
- `gpu_ms`: GL_TIME_ELAPSED interval, retrieved after completion.
- `completed_batch_frame_ms`: state mutation, submission and one final GPU drain,
  divided by frames; this is a batch throughput average, not individual frame tails.

CPU and GPU overlap: never add these intervals or turn them into application FPS.
Pixel reads, validation, negative controls and query retrieval are outside measured
intervals. Three interior RGB pixels per body are compared with expected tint,
including alpha over a separately rendered grid. Two interior pixels per
raster-covered wire are checked against exact red/green/blue state colors. At low
zoom, wire pixels are selected by pixel-center coverage inside the rectangle;
no color is inferred from an edge with anti-aliasing. Four RGB8 levels of tolerance
cover color quantization, blend rounding and small driver arithmetic differences.
A separate fixed-zoom fixture checks all nine AND/OR/XOR pin cores in OFF/ON/
disconnected states, rejecting wrong colors without confusing subpixel rim coverage.
All scales run explicit OFF/ON/disconnected controls; active cases also validate
both alpha states. Real grid-only, missing-body, missing-wire and wrong-state frames
must be rejected. The old blue-channel threshold has been removed.

## Source and run identity

An always-run CMake dependency captures a new manifest on EVERY benchmark build,
including source changes made after CMake configuration. It records SHA-256 hashes
of source/header files, benchmarks, CMake configuration, assets/shaders, vendored
and actual FetchContent dependency files, plus the external font. CMake cache,
Ninja build rules and compile commands are retained when available, along with
compiler, CMake, build tool, Git and Python versions, Git revision/status and UTC
timestamps. It embeds that snapshot in the executable, so a later build cannot
silently replace the identity of an older copied executable.

Each run writes the exact embedded bytes to `build_manifest.json`, records their
SHA-256 and independently hashes the running executable. It also stores complete
arguments, process ID, cwd, hardware, configuration and start/update/end timestamps.
Source drift since the build is recorded; rendering refuses changed runtime shaders
or font. SHA-256 is checked against empty, abc and million-a known vectors. Campaigns
recheck executable and manifest hashes, and tests reject deliberately altered bytes.
This is a reproducibility record, not a signed attestation or a guarantee against
concurrent source edits during compilation. Rebuild after editing before measuring.

## Reports and independent repetitions

```powershell
python benchmarks/report.py out/benchmarks/RUN_DIRECTORY
# Optional: --plot-packages LOCAL_MATPLOTLIB_DIRECTORY --output NEW_REPORT_DIRECTORY
```

Reports go into a NEW timestamped directory under `reports/`, or the explicit new
`--output`. Raw data and previous reports remain untouched. `summary.csv` pools
samples within this process; `summary-by-trial.csv` and HTML also show every trial.
Campaign `summary-by-process.csv` keeps independent executions separate instead of
pretending all trial samples are independent processes. No cross-process pool or
speed pass/fail threshold is imposed. Run IDs, raw trial/sample IDs and counts are
retained. p95/p99 use nearest rank and explicitly flag when rank=n makes the
percentile the maximum because of sample count. Small n is insufficient for strong
rare-stall claims. A report manifest identifies its raw inputs and report script.

## Additional measured coverage

`--suite coverage` runs:

- Actual atomic disk save/load for every requested component size. Complete parsed
  design JSON, component logical pins/edges, net counts and shorts are compared
  after propagation; allocation IDs may legitimately be renumbered. The file
  format persists authored design/manual inputs, not transient clock/latch state.
  OS file caches are warmed; these are application save/load costs, not guarantees
  about stable-media flush latency. Generated fixture files stay in the run directory.
- Inverter subcircuits nested one, two and four levels; full design roundtrip and
  every nested output verified after each timed input propagation. Size is depth.
- A real two-bit synchronous counter with D flip-flops/XOR feedback. Each trial
  measures 256 half-cycle steps, verifies every intermediate state/time and the
  complete output traces. This includes Scene synchronization, not GUI pacing.
- Bounded routing around 1/8/32 square obstacles, with independent segment-clearance
  checks; moving a branched source preserves both driven sinks and the junction.
  Obstacle size is obstacle count, not component count.
- A staged multi-operation batch that must roll back, plus a routing failure. The
  complete persisted design, runtime pins/edges, wire IDs/nets/states, revision,
  topology-build count, clock time/pending time, dirty state and result are unchanged.
  Expected rejected timings have their own `rejected_operation_ms` metric.
- Driven comb networks with exactly 1024/4096/8192/16384 normalized routes and 256
  components, 128 independent drivers/sinks. Degree-three branch points preserve
  route density; these are not disconnected padding wires. Rebuild is timed once
  per trial with phase decomposition. Save/load is timed once per process/count,
  then fully compared. These low sample counts deliberately have no tail claim.
  `size` means routes, with component/route counts also stored explicitly. Smoke
  uses a 64-route, 16-component network to keep development checks short.

Actual negative controls mutate a recovered label, manual input and wire geometry;
those circuits must fail the roundtrip and atomic-image validators.

`--suite lifecycle` repeatedly creates/copies/destroys 128-component scenes and
histories. Each scene runs three move/undo/redo/undo cycles and a complete design
comparison before all scenes/snapshots are destroyed. Memory is sampled after
those destructors. Twenty cycles warm allocator caches first. `--suite memory`
combines this with the existing bounded-history endurance test, each with its own
configured duration and checkpoints. Whole-process memory/handles and verified
cycle counts are observations, not a proof of leak freedom or absence of thermal
throttling. Choose longer durations explicitly for hour-scale investigations.

## Existing results and interpretation

The original `results/2026-10-15-release/` and first validated campaign remain
unchanged. Legacy rendering checks could accept the background; original passing
outcomes cannot be retroactively upgraded. New reports label this limitation.
Changes in observer/probe overhead, fixture composition or sampling count must not
be presented as simulator optimizations. Campaign summaries preserve each process
and each within-process trial instead of pooling them into fictitious independent
observations. For 120 samples, p99 is only the second largest observation; this
cannot establish a reliable rare-pause rate. Memory results apply only to the
recorded duration, workloads and machine. All algorithms and persistence limits
remain unchanged.
