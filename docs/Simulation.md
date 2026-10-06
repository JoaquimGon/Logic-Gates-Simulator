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
time must go through Circuit. Paused clocks accumulate no new phase. Manual step
still flips once and pauses; mark the circuit dirty to deliver that transition.

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
