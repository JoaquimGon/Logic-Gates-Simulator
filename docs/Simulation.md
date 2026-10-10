# Clock timing and feedback simulation

`Circuit` owns propagation and clock advancement. Connections still preserve both
pin indices and reject missing endpoints, invalid pins, and multiple drivers on
one input. Feedback, including self-connections, is accepted.

## Settling signals

`propagate()` refreshes source changes and seeds all components, then queues only
receivers whose inputs changed. Each step evaluates its components using the
current input values, collects output changes, and delivers the complete step
before the next evaluation. Component IDs provide deterministic ordering;
there is no topological sort. Contained subcircuits settle their private circuit
using the same evaluation budget, then publish their external outputs.

The circuit settles when the queue is empty. A call stops after 1,024 steps or
100,000 component evaluations. Reaching either limit returns
`SimulationResult::NON_CONVERGENT`, meaning signals did not settle within the
safety limit; it is not proof that every such circuit oscillates forever.
Automatic clocks pause, signal visuals become unavailable, and the warning stays
visible with debug metrics hidden. Changing an input, configuration, or wiring
requests another settling attempt. Invalid connections retain their separate
`CONNECTION_REJECTED` diagnostic.

This is a Boolean educational model with abstract propagation steps, not physical
gate delays, setup/hold timing, or metastability. Gate-built memory may need a Set
or Reset input to establish its initial state. A symmetric uninitialized feedback
loop can fail to settle. Existing SR/D latches keep their retained state; D latches
remain level-sensitive.

## Clock transitions

`updateClocks(seconds)` advances running clocks to the earliest due edge. All
clocks due at that instant change together, and the circuit settles before time
advances again. For example, a 1 Hz clock advances through both its rising edge
at 0.5 seconds and falling edge at 1 second during a one-second frame.

Clock phase uses double precision. `Clock::advanceSlice()` is an internal-use
primitive for a slice ending no later than the next edge; frame-sized elapsed
time must go through Circuit. Disabled clocks accumulate no new phase.
Frequency edits preserve fractional cycle phase. Circuit's global pause freezes
automatic time without changing individual clock enable settings. Pausing discards
pending catch-up time, so deliberate steps begin from the last processed instant.

Circuit's manual stepping advances to the earliest scheduled edge time and settles
the circuit. Two steps execute two consecutive edge times with a complete settling
call after each; simultaneous transitions count as one time. Stepping leaves
automatic time paused, excludes disabled clocks, and stops on simulation failure.
For one clock, two steps complete LOW→HIGH→LOW or HIGH→LOW→HIGH. With several
frequencies, two steps need not complete every clock's cycle.

One update processes at most 256 edge times, with a four-millisecond budget checked
between completed settling calls. A single settling call has its own bounds and
can exceed four milliseconds. Unprocessed elapsed time is retained for the next
update, including `updateClocks(0)`, so overload slows simulation instead of
silently skipping edges. Copies/snapshots retain clock phase and pending time.
Time while a non-convergent simulation is paused is discarded.

## Edge-sensitive receivers

A receiving component overrides `clockInputPin()` with its clock input index and
`onClockEdge(bool)` to act on rising (`true`) or falling (`false`) transitions.
The default clock input is `-1`; gates, latches, and clock sources have no edge
callback. The callback is delivered only when that pin's Boolean level changes,
including changes from gate-generated clocks.

All changed inputs in a step arrive before callbacks run. Callback output changes
reach other components in the following step. Registers sharing a direct clock
therefore sample before one register's new output reaches another's data input.
Data arriving in the same step as a clock edge is sampled at its newly delivered
value. Connecting a wire initializes its level without synthesizing an edge.
No new native flip-flop or component UI is added by this change.

`SimulationTests` verifies gate-built SR memory, stable/self feedback, bounded
oscillation and recovery, chronological/simultaneous clocks, retained catch-up,
rising/falling receivers, register ordering, generated clocks, and a master/slave
circuit built from existing D latches.


## Contained circuits

Each `Subcircuit` owns a deep copy of its definition's simulation template.
External input states drive the authored InputPins; authored OutputPins provide
indexed external output states. Layout and definitions remain separate from
runtime state. Scene connectivity rebuilds only the visible box's connections,
leaving its internal connections intact.

`collectClocks()` includes clocks inside nested instances in deterministic order.
The enclosing Circuit advances every clock at each chronological transition
before settling. Child circuits never consume frame time independently. Native
and contained evaluations share a 100,000-evaluation allowance; a child failure
marks the enclosing circuit non-convergent. Existing step and clock-time limits
still apply. See [Subcircuits.md](Subcircuits.md).

## Locating unresolved activity

Circuit retains one component ID when evaluation fails or bounded settling leaves
pending work. Copies retain it, and a successful propagation clears it. Scene/UI
use this small diagnostic to focus the enclosing visible component through FIND;
it identifies outstanding activity, not necessarily the feedback loop's original
cause. Shorted nets and rejected connections supply their existing pin/edge data.
No cycle tracing or new scheduling infrastructure is introduced.


## Clocks panel and chronograms

The permanent **Clocks** bottom tab is available in workspaces and subcircuit
editors. **Run/Pause**, **Half cycle**, and **Full cycle** control the shared timeline
in either editor mode. Space/Period remain canvas shortcuts in Interaction mode,
using the same pause/one-edge behavior. Half cycle advances to the next rising
or falling transition; Full cycle processes two transitions. These names describe
one enabled clock. With several clocks, the buttons process one/two shared edge
times, so Full cycle need not complete a cycle for every clock.

Each row shows a live name, HIGH/LOW level, frequency, and Enable/Disable control.
Click a native clock's Hz field, type a finite frequency of at least 0.1 Hz, and
press Enter. Typing replaces the displayed value; Escape cancels. Frequency and
enable edits use existing EditorActions, saving, and undo/redo. Runtime pause,
stepping, chart zoom and scrolling do not dirty the saved design.

Rows inside nested components read the actual placed instance, with a distinct
ID path per clock. Their settings are read-only here; edit their source subcircuit
to change them. Names use the authored layout's labels, mapped to compiled IDs.

The green digital waveforms use processed simulation seconds, not rendered frames.
Every clock transition is recorded, even when several occur between frames.
Each clock's edge guides and blue current-time marker stay inside its own row.
The centered **Time frame = N s** label states the visible duration; the toolbar's
Elapsed value is processed simulation time. There are no axis/cursor lines when
the circuit has no clocks.

**Fit** (the initial view) shows four cycles of the fastest enabled clock:
time frame = 4 / Hz, bounded by the existing zoom limits. At 256 Hz this is
0.015625 seconds. It follows frequency/enable changes; if all clocks are disabled,
it uses the fastest configured clock. **−/+** double/halve the duration and stop
automatic fitting. **Fit** restores it. Scroll moves between clocks, Shift+scroll
moves through history, and **Live** follows the newest time without changing zoom.

Unreadable rows show **Too fast for this time frame. Zoom in or Fit**, with no
waveform, edge-guide, or cursor geometry. A row needs at least eight logical pixels
per half-cycle and at most 128 visible edges: 2 × Hz × time frame must not exceed
min(128, graph width / 8). At 256 Hz over four seconds, drawing is skipped outright.
Fit or zooming in restores the waveform when the displayed edges are readable.
The same bounds apply to actual retained history, including earlier higher rates.

Suppression happens before waveform traversal/geometry generation, rather than
drawing a dense stripe or shaded block. Recorded edges and simulation behavior
remain exact. Clock paths are collected once per update and reused during edge
recording, rather than sorted/copied every edge. Simulation still has to process
each clock edge within its existing work limits.

**Clear**, at the far right, removes this view's recordings and starts a fresh
baseline at the current time. It changes neither clock state/phase nor simulation
time, frequency, save status, or edit history. Recording continues normally.
Hover a toolbar/enable button for 0.75 seconds to see its explanation, including
shared-timeline stepping, simultaneous edges, and disabled/blocked clock warnings.

ClockTimeline retains at most 2,048 state samples per clock and 2,048 shared edge
guides per view. It is separate from Scene snapshots and JSON files. Switching
views retains history; opening a file, undo/redo, or replacing published definitions
resets the affected history. Evicted time ranges are left blank. There is no signal
analyzer, physical timing model, or new scheduler framework.
