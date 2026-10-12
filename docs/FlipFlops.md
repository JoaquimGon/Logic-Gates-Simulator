# Native flip-flops

D, T and JK flip-flops are built-in cards after the latches in the Native panel.
Each stores one bit, initially **Q=0** and **~Q=1**. They update only when **CLK
changes from LOW to HIGH**. Data changes while CLK stays HIGH or LOW, and falling
edges, leave the stored bit unchanged. Unlike the existing D latch, they are not
transparent throughout a HIGH clock level.

| Component | Inputs | Behavior at a rising edge | Useful exercise |
| --- | --- | --- | --- |
| D | D, CLK | Q becomes D | One-bit register; connect several stages for a shift register |
| T | T, CLK | T=0 holds; T=1 toggles Q | Divide a clock's frequency by two; build a counter |
| JK | J, K, CLK | See the table below | Learn set, reset, hold, and toggle behavior |

| J | K | Next Q |
| --- | --- | --- |
| 0 | 0 | Previous Q (hold) |
| 0 | 1 | 0 (reset) |
| 1 | 0 | 1 (set) |
| 1 | 1 | Opposite of previous Q (toggle) |

Connect Input components to the data pins, a Clock to CLK, and Output bulbs to Q
and ~Q. Pause automatic clocks and use **Half cycle** to inspect individual edges.
After a rising edge, change the data while CLK is HIGH: Q should stay unchanged.
Another half cycle falls; the following half cycle rises and samples new data.

Registers sharing a clock capture the values available before their outputs
change, so one rising edge advances a shift register by one stage. Wiring or
rotating a component does not manufacture a clock edge. All four orientations
use the same logical indices. Copying editor state/undo preserves the stored bit;
saving/loading designs and creating subcircuit instances start with fresh memory.
Each placed subcircuit has private flip-flop state.

Simulation Reset resets sources and clock phases; a falling clock transition
leaves these rising-edge flip-flops' stored bits intact. Use a rising edge with
D=0, or JK=01, to explicitly clear the corresponding stored bit. T can hold or
toggle its bit. There are no asynchronous preset/reset pins or real-world
setup/hold timing delays in these simple educational components.
