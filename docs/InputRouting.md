# UI and Canvas Input Routing

`Input` adapts GLFW callbacks; `InputRouting.cpp` owns UI capture, focus, bounds,
and interruption policy. Gesture state stays in `Editor/Gestures/`. The application's
custom UI handles menus, panels and popups before forwarding unconsumed events to
the canvas. The adapter contract below also supports other UI integrations.

## Connecting a UI adapter

Install one `UiInputHandler` with `Input::setUiInputHandler()`. Every key (including
repeats/releases), Unicode character, mouse button, cursor movement, scroll, and
window-focus event reaches this handler before canvas handling. `UiInputEvent`
uses GLFW integer codes without requiring GLFW headers. Cursor/button positions
are window coordinates with a top-left origin; scroll coordinates are offsets.

The following illustrates a toolkit adapter (`ui` represents your chosen toolkit):

```cpp
input.setUiInputHandler([&](const UiInputEvent& event) {
    ui.handleEvent(event);
    input.setUiCapture({ui.wantsKeyboard(), ui.wantsPointer()});
    return ui.consumed(event);
});
input.setCanvasViewport(CanvasViewport{240, 48, 960, 720});
```

Returning `true` consumes that event. Publish persistent keyboard/pointer capture
with `setUiCapture()` as UI ownership changes, including changes outside callbacks.
An adapter must set capture or call `setCanvasFocused(false)` **before** submitting
an editor action inside its handler, so a conflicting drag preview is cancelled
before the action validates. Apply structural edits through `EditorActions`.
Replacing/unregistering the handler cancels unfinished input and resets capture;
its owner must unregister before destroying captured adapter objects.

## Ownership and cancellation

Keyboard shortcuts require canvas focus and an uncaptured keyboard. This includes
F2/F3, creation/deletion, Escape, and clock controls. Consumed key/text events and
keyboard capture transfer focus away from the canvas. Ending capture does not
restore keyboard focus automatically: a fresh uncaptured click inside the canvas
restores it, or the UI can explicitly call `setCanvasFocused(true)`.

Keyboard and pointer capture are independent: pointer capture alone does not
block focused keyboard controls. Creation/deletion additionally require an eligible
canvas pointer for placement/hit testing.

Pointer capture blocks component actions, dragging, wire drawing, panning, and
hover. Starting capture, transferring focus, changing bounds, or leaving the
canvas cancels unfinished gestures and queued shortcuts. Drag previews restore
committed placement; provisional wires are discarded. Panning stops at its latest
offset. Selection is preserved so an inspector can edit it. Escape, mode changes,
scene changes, and OS focus loss retain their existing deselection behavior.

Canvas-owned mouse presses are tracked separately from physical button state.
A release after cancellation cannot finish a gesture; held keys/buttons cannot
restart one when capture ends. Releases still clear physical state while captured.
OS focus loss also clears pressed state; unfocused windows cannot edit the canvas.

## Canvas viewport

`Input::setCanvasViewport()` now supplies the shared camera layout for both input
and rendering. It replaces the RM-U1 input-only `setCanvasInputBounds()` name.
Logical GLFW coordinates are clipped to the window and snapped to framebuffer
pixel edges; those effective bounds gate input and scissor every world pass.
Left/top edges are inclusive; right/bottom edges are exclusive. `std::nullopt`
selects the whole current window; empty/minimized canvases accept no pointer input.
Negative/non-finite bounds throw before changing active input. Window/framebuffer
size changes cancel gestures before interpreting new coordinates.

See [CanvasCamera.md](CanvasCamera.md) for camera ownership, forward/inverse
conversions, DPI handling, and screen-space overlay behavior.

The `UI` adapter handles the left palette and right-click information popup.
An idle component/pin right-click consumes its press/release, preserving canvas
selection and preventing panning from that click. Right-click during a canvas
gesture still cancels it; middle-mouse drag pans, including over component bodies.
Right-drag no longer pans. Popup-local pointer
events cannot operate components underneath. Outside left-click dismisses the
popup without starting a gesture; outside right-click can inspect another
component. Outside middle-click dismisses the popup and begins a pan. Escape
dismissal and the opening right-button release restore canvas keyboard focus.
Both editor modes can inspect; simulation and
focused clock controls continue. Resize, focus loss, and component deletion
dismiss the popup. `component_information` tests these interactions headlessly.

## Verification

Three GLFW null-platform regression groups exercise event payloads, typing and clock
isolation, focus recovery, held-key cleanup, UI-first dispatch, drag/wire/pan
cancellation, releases after cancellation, and valid/invalid canvas bounds:

```sh
ctest --preset x64-debug -R "ui_input_routing|ui_gesture_cancellation|canvas_input_bounds"
```

These require no display or OpenGL context. They establish the routing contract;
toolkit-specific focus and widget behavior need integration checks when UI is added.

## Area and group selection

In Selection mode, Shift-left-drag starts an axis-aligned world-space rectangle.
The existing CanvasCamera converts pointer positions, so pan, zoom, viewport
bounds and DPI agree with drawing. Starting the rectangle over a component or pin
still selects instead of moving/connecting it. Release replaces the selection
with fully contained visible body bounds and whole wire paths; partial overlap
is excluded. Direction does not change containment. The gesture remains owned
until left release, even if Shift is released first.

Ctrl-left-click toggles a whole normalized wire section, preserving selected
components/other wires and avoiding branch creation. Ordinary wire clicks keep
the existing single-segment/branch behaviour. Dragging a selected component or
Ctrl/box-selected wire moves the selected objects by one snapped offset.
Explicitly selected wires keep their bends and translate with the group; external
wires follow selected pin positions. Alt at release disables pin-following routing,
while explicitly selected wires still move as selected objects. Moving a wire section by itself leaves unselected neighbouring sections fixed;
matching geometry reconnects on drop. Delete/Backspace submits the entire selection in one edit.

World-space rectangles reuse the existing rounded outlines. Fully enclosed objects
show a live 35% opacity preview while dragging, which becomes solid on release.
Candidate IDs update on pointer movement without changing the committed selection
or undo history; excluding an object removes its preview. The pending rectangle
adds a 6% fill. UI capture, focus loss, Escape, scene/mode changes
and canvas layout changes cancel unfinished gestures. UI capture/layout interruption
preserves the prior completed selection; Escape/mode/scene changes clear it.
Simulation and saving read committed geometry throughout a group preview.

`box_selection`, `group_movement`, `group_deletion`, `group_cancellation`, and
framebuffer checks cover selection boundaries, reverse direction, modifiers,
relative movement, translated wire shape, external rerouting, normalized wire
selection, one-rebuild deletion, overlap rollback, focus/capture cancellation,
and unobtrusive outlines/rectangle fill at normal and 2x scaling.

## History shortcuts

With canvas keyboard focus in Selection mode, Ctrl+Z invokes the active scene's
history; Ctrl+Y and Ctrl+Shift+Z redo. A pending move, wire, pan or box gesture is
cancelled first without consuming a committed history step. Selection and queued
canvas shortcuts are cleared before restoration. UI typing/menu capture prevents
circuit-history shortcuts from running. Committing a component name restores
keyboard focus on Enter release so the next Ctrl+Z can undo that commit immediately.
History recording uses the same Input result handler for keyboard, gesture,
palette and naming edits; each tab retains its own bounded history.

## Messages panel

The bottom Messages tab is available in both workspaces and subcircuit editors.
It shows **current circuit problems**, replacing the earlier accumulated error
history. If none remain, it displays `Circuit is fine.` The latest failed action
is listed separately with its local `[HH:MM:SS]` timestamp; another action replaces
it, and a successful edit/file operation dismisses it. There is no manual Clear
button; simulation problems disappear when their conditions are repaired. The Subcircuit tab still owns interface readiness
and vital-component names, and remains hidden in workspaces.

Simulation issues persist while their condition exists and disappear on repair,
without keeping old errors or recovery messages. These are view-local UI state,
excluded from circuit saving and undo history. Messages shows a persistent active-problem count, including the latest failed
operation. Its text/underline is red for errors and amber when only warnings
remain; reading does not clear this indicator, and resolution removes it.
The underlined navigation notice opens Messages on click, dismissing popups and
preserving the active circuit, camera, completed selection and history. New
problems never switch the bottom tab automatically. A narrow bar uses the shorter
`See Messages` link, leaving the right-side unsaved status visible.
Feedback itself is valid. Only non-convergence, rejected wiring, multiple drivers
on one net or sustained processing backlog appear as circuit problems. Backlog
requires more than 100 ms of pending time for one second of active processing.

Located errors end with a clickable `FIND`. It centers CanvasCamera on a
conflicting output pin, rejected connection or component with remaining simulation
activity, preserving zoom, scene data, selection and history. Pending activity is
not a proof of the original cause; nested failure points to the enclosing custom
component. Global backlog has no guessed gate location. Links are disabled while
an edited circuit awaits evaluation; removed/repaired errors lose their links.
Wrapping, mouse-wheel scrolling and FIND hit testing share the same row geometry
and canvas/DPI transforms. F3 remains an explicitly opened debug overlay.


Clocks controls work in either canvas mode. Frequency editing captures keyboard
input until Enter/Escape; outside clicks, focus loss, resize, view switches and
scrolling cancel the draft. Button presses/releases and panel scrolling stay in
UI. Shift is included in scroll-event modifiers for time-history navigation.
Space/Period remain focused Interaction-mode shortcuts, using the same shared
pause/chronological one-edge operation. Input binds the active view's trace
recorder and clears its history on successful undo/redo.


The bottom panel's top edge captures pointer and keyboard input while resizing,
in either Selection or Interaction mode. Moving it updates the shared canvas
viewport immediately without editing scenes or recording undo history. Release,
Escape, focus loss, window/DPI resize, or switching viewpoints ends the drag at
its current height. Completed canvas selection is preserved. Popup and palette
interactions retain ownership; resizing does not intercept their gestures.

## Copy and paste

In Selection mode, Ctrl+C snapshots the committed selection into one session-local
`EditorClipboard`; empty selection leaves the previous copy intact. Ctrl+V starts a
`PasteGesture` with an isolated copy and the existing move-preview API. The translucent
group follows the cursor; overlap checks use the same body/origin/pin rules as a real
edit and show an amber advisory highlight for overlapping components. Overlap does
not block paste unless component grid centers or pin positions coincide. Those
collisions show red and leave the preview active on a placement click.
Electrical conflicts remain visible in the usual simulation diagnostics. Moving the preview never rebuilds
the real scene's nets, advances copied clocks, or records history. Geometry/overlap
checks are reused while pointer cell and destination revision remain unchanged.

Left-click submits the creation batch once and selects the new components and
normalized wire sections. Pasted creation requests use `RejectSharedAnchors`, allowing
body overlap while rejecting identical grid centers or coincident pins in the final
batch, including within the copied group. Palette placement, single/group dragging
and component settings use the same restriction. Keyboard spawning advances one
diagonal cell while a center or pin position conflicts, without requiring body clearance.
Invalid coordinates or incompatible definitions still reject placement without changing
the circuit, and leave the preview available to reposition/cancel.
Escape/right-click cancels; Ctrl+Z during preview cancels without undoing an earlier
edit. Each confirmed paste records one undo step; cancellation/failure preserves redo.

The clipboard survives tab changes and source deletion. Labels, explicit pin
geometry, gate inversion/arity, input values, and clock settings are retained;
custom definitions travel with copied instances. Clock phase and latch memory start
fresh. Existing custom definitions with a different version reject paste rather
than reinterpret the copied design. Only explicitly selected wires travel with
components; a selected ordinary wire segment copies just that segment.

Both shortcuts require an idle Selection canvas with keyboard focus. Starting/placing paste also
requires the cursor inside the canvas. UI capture, focus/scene/mode changes and
leaving the viewport cancel pending/active paste previews; key repeats do not duplicate objects.
No system clipboard file format is introduced.

## Canvas label visibility

Every component information popup includes **Show label: On/Off**. Selection mode
can toggle it through `ConfigureComponentProperties::labelVisible`; Interaction
mode and active text editing keep the button read-only. This changes only body-label
presentation, preserving the stored name, pin captions, signal state and connectivity.
It records one ordinary undo step without rebuilding topology. Copy/paste and saved
instances retain the choice. Native clock definitions default to hidden labels;
older JSON files without the optional flag retain their previous defaults.

Native logic-gate input rows in the right-click popup include an **Invert: On/Off**
button. Selection mode submits a `ConfigureComponentProperties::inputInversion`
with the pin index and flag; Interaction mode shows the same state read-only.
The added input cell is shared by the whole bank while any input is inverted.
Each bubble occupies its pin's added cell; raw input values remain unchanged and
only gate evaluation complements them. Edits reroute attached endpoints and use
one history entry. Removed inputs lose their flags, appended inputs start normal.

The gate popup shows at most four input rows on a subtle background. Scroll over
that list to reach additional inputs; the heading shows the visible range and a
scrollbar marks the position. Gate settings and the output state stay fixed.

Wire routing permits interior crossings without electrical junctions. It continues
to avoid unrelated whole-wire endpoints, collinear overlaps, and component padding.
Releasing on a wire intentionally joins it. Branches may return to the same input
or another pin on their component, including undriven branches and feedback.
Connectivity and bounded propagation validate the result; blocked routes still
report an editor error. Duplicate overlapping geometry is normalized as usual.

Logic-gate, latch and subcircuit popups offer Up, Down, Left and Right orientation
buttons. Only logic gates show input-count and inversion controls.
Right is the default; Selection mode changes it and Interaction mode displays it
read-only. Orientation is one explicit editor property. Pin anchors and lead
waypoints remain in the authored right-facing frame; absolute picking, topology,
bounds and rendering apply the instance's quarter turn. Arity and inversion edits
therefore retain their existing local layout rules. Rotation reroutes attached
endpoints in the same undoable edit. Saved instances and clipboard data retain
orientation; older circuit files default to Right. Subcircuit rotation affects
only the placed box and interface pins, preserving its authored internal scene.


Edit/View menus reuse the File dropdown's capture/dismissal rules. Menu opening
preserves committed selection while cancelling pending gestures. Edit commands
share InputCommands with keyboard input; deletion still goes through EditorActions
and undo/redo still use the active view's existing history. Click actions execute
after mouse release so UI capture cannot cancel a newly started paste preview.
Menu Paste waits outside the canvas until the pointer enters it; later cancellation
retains the ordinary paste rules. Disabled entries cannot execute.

Ctrl+A selects all component IDs and whole normalized wires. Home fits all visible
body bounds and routes; F centers the current selection without changing zoom;
Ctrl+0 restores zoom 1. Camera operations retain selection and do not change scene
revision/history. Ctrl+J toggles the lower panel through its existing layout;
F3 and View/Debug Overlay share one UI flag observed by Engine. Text-field capture
prevents these shortcuts from escaping, and F3 does not interrupt canvas gestures.
