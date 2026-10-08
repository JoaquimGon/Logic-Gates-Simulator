# UI and Canvas Input Routing

`Input` adapts GLFW callbacks; `InputRouting.cpp` owns UI capture, focus, bounds,
and interruption policy. Gesture state stays in `Editor/Gestures/`. No UI toolkit
or widgets are installed yet: the default handler passes events to the full-window
canvas, preserving current controls.

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
excluded from circuit saving and undo history. New problems mark an unopened tab
with `*`; ongoing pause/short/backlog status also appears in the navigation bar.
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
