# Educational truth tables

Right-click a gate, latch, flip-flop or custom circuit, then select **Truth table**. **Info / settings**
returns to its existing controls. Tables work in Selection and Interaction modes.

## Logic gates

Columns use logical pin order and the component's pin names. Rows enumerate external
input levels from all LOW to all HIGH. A `*` on an input heading marks an inverted
input; the result includes both input bubbles and output inversion. XOR computes
odd parity across all inputs; NXOR is its inverse.

Native gates support up to eight inputs (256 combinations). At most eight rows are
drawn; scroll over the popup to see the rest. The footer shows the visible range and
a scrollbar indicates more rows. **Current row** brings the current combination
into view. Opening the table also reveals it; subsequent input changes update the
green highlight without interrupting manual scrolling.

The `Now` and `Outputs` lines show actual synchronized pin levels. Dirty or failed
simulation does not highlight a stale row; unavailable values appear as `?`.

## Latches and flip-flops

These show **next-state tables**, not combinational truth tables. In result cells,
`Q` and `~Q` refer to the stored output values: `Q, ~Q` holds; `~Q, Q` toggles.
The separate Outputs line always shows the current stored bit.

- SR latch: `00` holds, `01` resets, `10` sets. `11` is invalid and this simulator
  forces both outputs LOW; subsequent `00` preserves both stored values.
- D latch: Enable LOW holds; Enable HIGH copies D to Q.
- D/T/JK flip-flops: the table describes the next **CLK 0 → 1** transition.
  CLK is shown in the live inputs, but is not enumerated as a data column.
  Falling edges and changes while CLK remains HIGH/LOW hold state.

The green row identifies the rule for the **current inputs**, not the last sampled
edge. For example, a D flip-flop may show D=1 and Q=0 until the next rising edge.

## Implementation boundary

`UI/TruthTable` contains a small read-only table structure. Gate results are obtained
from an isolated component copy; the live scene, clock phase, history and stored
state are untouched. Tables are cached for the selected component/design revision;
live row selection reads synchronized pins. Rendering stays bounded by visible rows.

Sources and output indicators do not advertise a table. Components above eight
inputs show **Truth table too big**, with the input limit and actual count.

## Custom circuits

Custom combinational circuits use their named external inputs and outputs as table
columns. Every output is recorded for every input combination, including behavior
inside nested custom circuits and configured gate inversions. Calculation uses a
deep copy of the placed circuit and preserves live internal/external pin states.
The existing scrolling and current-input highlighting also apply to these tables.

Before calculation, the internal circuits are checked recursively. Clocks,
latches, flip-flops, unsupported component behavior and feedback connections show
an explanation instead of a table. This first version rejects feedback even when
the current circuit settles: current stability does not establish a stateless
input/output function for every combination and starting state. Native latches and
flip-flops still have their explicit next-state tables.

One shared work limit of 100,000 covers structural inspection and all internal
component evaluations across every row, including nested circuits. If the limit is
reached or a row fails to settle, all partial rows are discarded and the popup
explains why no reliable table is available. Results are cached for the selected
component/design revision and calculated when its table is requested.
