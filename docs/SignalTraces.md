# Signal traces

Right-click a component, then click **Trace** beside an input or output. Click
**Stop** to stop watching it. This works in Selection and Interaction modes,
and inside a placed subcircuit's read-only live view. Each workspace/subcircuit
editor can watch up to eight pins. Open **Signals** in the bottom panel to see
names, current LOW/HIGH states and waveforms; **X** removes a row.

Signals use the same simulation time and controls as Clocks. Run/Pause and
Half/Full cycle operate the circuit's enabled clocks. Clear clears both graph
histories while keeping selected probes and current circuit state. Fit uses the
shortest recorded positive interval between signal changes, multiplied by eight
and rounded up to a power-of-two frame (0.5, 1, 2, 4, 8 seconds, etc.), within
the existing zoom limits. Recorded timestamps stay exact. Signals start with a
two-second frame and keep their scale until Fit or zoom is pressed. New signal
changes, probe changes and clock edits do not automatically rescale the frame.
Live follows the newest recorded time; Shift + scroll browses history.
Signals retain their history-only layout, with current time at the right edge once
the time frame is full. Only the Clocks tab centres current time and previews edges.

Recording happens after settling at each scheduled clock edge, including edges
between rendered frames, and after manual input changes settle. Input traces
show the electrical signal before any inversion bubble. Unwired inputs and
failed/unsettled simulation states are unavailable and draw a gray middle line.
Propagation rounds have no modeled physical delay and are not plotted as glitches.

While clocks are paused, disabled or absent, manual changes share the same
simulation timestamp. The current state still updates and the row says
“paused changes”; this graph does not invent elapsed time to space them apart.

Each trace retains at most 2,048 state changes. Dense visible regions are hidden
with a zoom/Fit hint; rendering inspects/builds at most 128 visible changes per
row. Selected probes/history are session-only and do not alter saves or undo.
Design edits clear recorded signal history. Surviving top-level pin identities
remain selected; deleted pins disappear. Probes inside live instances are removed
after parent design edits/publication because compiled internal IDs can change.
Select those internal probes again in the updated live view.
