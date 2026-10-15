# Circuit examples

Open **File > Examples** and choose a circuit. Hovering or clicking Examples
expands the submenu. Each choice opens a fresh editable workspace tab and fits
the circuit into the canvas. Existing tabs, unsaved work and file associations
stay intact. Use Save or Save As to keep your own version.

Examples start in Interaction mode. Click the labeled inputs to toggle them;
switch to Selection mode to modify the circuit.

- **Logic gates:** compare AND, OR and XOR with the same A/B inputs.
- **Half adder:** add two bits; Sum is A XOR B, Carry is A AND B.
- **Full adder:** add A, B and Carry in using two XORs, two ANDs and an OR.
- **2-bit counter:** two D flip-flops share a 1 Hz clock. Bit 0 feeds its inverted
  output back to D; Bit 1 feeds Q1 XOR Q0 back to D. The outputs count 00, 01,
  10, 11 on rising clock edges. The Clocks panel opens with the clock paused. Press Run or
  Half cycle; falling edges do not advance the count. Simulation > Reset
  Simulation restores zero.

The menu stays inside the window; on narrow windows it expands below its parent.
Escape closes the Examples submenu first, then the File menu. Example geometry
is generated from the native component definitions and routed with the same
clearance rules as editor wires. Truth tables, count/reset behavior, save/reopen,
menu input and workspace preservation are covered by `example_circuits`.
