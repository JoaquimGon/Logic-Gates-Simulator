# Educational truth tables

Right-click a gate, latch or flip-flop, then select **Truth table**. **Info / settings**
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

Sources, output indicators and composed subcircuits do not advertise a table.
Subcircuits may contain retained state and clocks; enumerating input combinations
alone would misrepresent their behavior. Gates above eight inputs show an explicit
limit message rather than generating an unbounded table.
