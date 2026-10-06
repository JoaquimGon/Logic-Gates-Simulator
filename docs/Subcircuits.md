# Reusable subcircuits

Create a **Subcircuit editor** with the tab strip's + menu. Inputs and outputs
become the external interface; give each a distinct name within its direction.
Their pin order follows component creation order and is recorded in JSON.
Clocks remain internal. The bottom Subcircuit panel reports interface problems
and lists input/output/clock names, including qualified names for nested clocks.

## Saving and placing

- In a subcircuit editor, File shows **Save (subcircuit)** and
  **Save As (subcircuit)**. The first Save asks for a path; later saves reuse it.
  Save As always chooses another path and then remembers it for that editor.
- Save accepts unfinished drafts. A valid, settling design is also published
  to **Custom**. Shorted/rejected wiring, missing ports, duplicate names, or an
  unstable simulation prevent publication, while the draft remains saved.
- In a workspace, **Load subcircuit** imports a standalone JSON file. Valid
  definitions appear in Custom; invalid drafts open in an editor instead.
- Drag a Custom row onto the canvas. Each placement has its own input values,
  latch memory, clock phase, and contained circuit. Boxes use the existing shader
  and display the circuit name and external pin labels.
- Right-click a placed box and click **Edit subcircuit**. This reopens the original
  components, positions, settings, and wire bends. An already-open editor is
  selected without replacing its unsaved work.

Publication occurs on explicit Save, not on every edit. Compatible saves update
direct placements across open views and reset those placements' runtime state.
Changing the number, names, or order of external pins while instances exist
rejects publication atomically. The new draft still saves, and the previous
published definition/wiring stays intact. Remove the old placements before
publishing a changed interface; automatic wire migration is not implemented.

Nested subcircuits retain the dependency versions saved inside their authored
design. Saving a child updates its direct placements in open parent editors;
save the parent to publish its updated snapshot to Main. There is no automatic
dependency watcher. Direct or indirect self-containment is rejected. Packaging
allows up to eight nested levels and 4,096 expanded components per definition.

## File and simulation behavior

Standalone `logic-gates-subcircuit` JSON contains a stable `subcircuit.*` ID,
definition version, ordered port mapping, and a complete editable circuit design.
Save As preserves that definition ID; it creates another file copy, not a new
independent component type. Main's circuit JSON embeds every used definition and
its authored dependencies. External files are unnecessary to run or reopen a
saved Main design. Camera state and unsaved editor drafts are not packaged.

Placed interfaces start with false inputs driven by their enclosing circuit;
authoring test input values and retained memory do not initialize placements.
Clocks in all nested placements share the enclosing circuit's chronological
schedule. Global pause/step/frequency controls also reach internal clocks.
Settling shares the 100,000-evaluation limit across contained circuits; unstable
feedback stops the enclosing simulation. Design loads reset latch memory and
clock phase, just like native circuit saves.

`SubcircuitTests` covers indexed ports, independent state/copies, nested clocks,
bounded feedback, draft/interface validation, embedded round trips, publication
rollback, Save/Save As paths and failures, contextual menus, palette placement,
and Edit dispatch. The full Debug suite passes 56 groups and 13 targeted
AddressSanitizer checks pass. Framebuffer previews include `subcircuit-custom.ppm`,
`subcircuit-edit.ppm`, and `subcircuit-file-menu.ppm`. Native picker interaction
remains a manual application check.
