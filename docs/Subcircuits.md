# Reusable subcircuits

Create a **Subcircuit editor** with the tab strip's + menu. Inputs and outputs
become the external interface; give each a distinct name within its direction.
Their pin order follows component creation order and is recorded in JSON.
Clocks remain internal. The bottom Subcircuit panel reports interface problems
and lists input/output/clock names, including qualified names for nested clocks.

## Saving and placing

- In a subcircuit editor, **Save to library** stores the full editable design in
  app-managed JSON files, without a file picker. Published designs automatically
  appear in **Custom** on future launches.
- Save accepts unfinished drafts and drafts whose new wiring cannot be routed safely. **Draft:** rows in Custom open
  their saved design for editing; they cannot be placed. A previous usable
  published version remains available until a valid Save succeeds.
- **Export subcircuit...** writes a portable JSON copy. It does not change the
  managed Save destination or clear pending library changes.
- **Import subcircuit...** copies a standalone JSON design into managed storage.
  Moving or deleting the imported source file does not remove the library copy.
  Invalid imports open as saved drafts instead of becoming placeable components.
- **Open storage in Explorer** opens the managed folder. **Change storage
  location...** copies published designs and drafts before remembering the new
  folder; original files remain intact. Conflicting destination files abort the
  change. The default is `%LOCALAPPDATA%/LogicGateSimulator/storage/`; the small
  location setting remains in `%LOCALAPPDATA%/LogicGateSimulator/settings.json`.
- Drag a Custom row onto the canvas. Each placement has its own input values,
  latch memory, clock phase, and contained circuit. Boxes use the existing shader
  and display the circuit name and external pin labels.
- Right-click a Custom row, or a placed box and click **Edit subcircuit**. This reopens the original
  components, positions, settings, and wire bends. An already-open editor is
  selected without replacing its unsaved work.

Publication occurs on explicit Save, not on every edit. Saves update direct
placements across open views, including their box size, pin labels and logic,
and reset those placements' runtime state. Surviving ports retain their wires
even after renaming or changing pin order. Adding ports reroutes existing wires
to their new anchors. New ports receive their own identities and do not inherit
removed connections. Removing a port leaves its
wire end disconnected just outside the box, rather than attaching it to a
replacement port. Instance position, orientation and custom body labels remain.

Each authored input/output has a small persistent `port_id`, separate from its
visible label and compiled pin index. Save/reopen preserves it; older files
receive their previous direction/order identities when loaded. A deleted and
recreated port is a new port even if its name is unchanged. If nearby components
or wires prevent safe migration, publication still rejects atomically and saves
the draft; the existing usable definition and its wiring remain intact.

Nested subcircuits retain the dependency versions saved inside their authored
design. Saving a child updates its direct placements in open parent editors;
save the parent to publish its updated snapshot to Main. There is no automatic
dependency watcher. Direct or indirect self-containment is rejected. Packaging
allows up to eight nested levels and 4,096 expanded components per definition.

## Viewing a running instance

Right-click a placed box and select **View inside (live)**. The temporary view uses
the original authored layout and shows that specific instance's current gate,
input/output, latch/flip-flop and clock signals. Two copies of the same definition
can display different retained states. Powered internal wires follow the running
instance; right-click internal components for their live information/truth tables.

The top strip names the instance path and provides **Back**. Escape closes a
component popup first, then returns one level. Nested boxes also offer View inside.
Each Back restores the containing view's pan/zoom. Normal panning, zoom and Fit
Circuit remain available; opening the viewer fits the authored layout.

This is read-only observation: component edits, manual input toggles, wiring,
creation/deletion, clipboard/history edits, clock settings and saving the displayed
layout are disabled. The parent circuit continues running. The **Clocks (parent)**
panel retains Run/Pause and Half/Full-cycle controls for following edges; Messages
also belong to the parent, and FIND is available again after returning to it.

Viewing creates no editable tab and does not change saved state or runtime memory.
Changing workspaces exits the viewer. Parent design edits/publication dismiss it
before stale instance references can be read. Layouts are copied once on entry;
signal updates read the actual private runtime using the authored-to-compiled ID
mapping recorded during packaging. No second simulation is advanced.

## File and simulation behavior

Standalone `logic-gates-subcircuit` JSON contains a stable `subcircuit.*` ID,
definition version, ordered port mapping, and a complete editable circuit design.
Export preserves an existing definition ID; it creates a portable copy. Main's circuit JSON embeds every used definition and
its authored dependencies. External files are unnecessary to run or reopen a
saved workspace design. Opening a workspace retains its embedded versions locally;
it does not publish them into other scenes or replace the managed library.
Camera state and unsaved editor drafts are not packaged.

Destructive Open, dirty-tab closure, and application exit ask **Save / Discard /
Cancel**. Exit lists dirty tabs together and offers Save All. A cancelled picker
or failed Save aborts the destructive operation; already successful saves are
kept. Save All also saves workspace/parent changes published by subcircuit saves. Unsaved designs are protected by these prompts, not by crash recovery.
Only the active scene is simulated, and simulation time pauses while the prompt
is open.

Placed interfaces start with false inputs driven by their enclosing circuit;
authoring test input values and retained memory do not initialize placements.
Clocks in all nested placements share the enclosing circuit's chronological
schedule. Global pause/step/frequency controls also reach internal clocks.
Settling shares the 100,000-evaluation limit across contained circuits; unstable
feedback stops the enclosing simulation. Design loads reset latch memory and
clock phase, just like native circuit saves.

`SubcircuitTests` covers indexed ports, independent runtime, authored JSON,
publication rejection, library restart, drafts, import independence, storage
relocation/conflicts, malformed-file isolation, and cancelled/failed Save guards.
Native file and folder pickers remain a manual application check.
