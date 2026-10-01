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
input.setCanvasInputBounds(CanvasInputBounds{240, 48, 960, 720});
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

## Bounds and remaining camera work

Bounds use logical GLFW window coordinates, not framebuffer pixels. Left/top edges
are inclusive; right/bottom edges are exclusive. `std::nullopt` selects the whole
current window, while a zero dimension disables pointer input. Negative/non-finite
bounds throw without changing active input. The window extent always clips input.

Bounds only gate input. **RM-U2 still owns camera transforms, viewport rendering,
clipping, and mouse-to-world conversion for a smaller canvas.** Setting bounds
alone does not reposition or resize the rendered circuit. UI widgets and property
schemas remain separate roadmap tasks.

## Verification

Three GLFW null-platform regression groups exercise event payloads, typing and clock
isolation, focus recovery, held-key cleanup, UI-first dispatch, drag/wire/pan
cancellation, releases after cancellation, and valid/invalid canvas bounds:

```sh
ctest --preset x64-debug -R "ui_input_routing|ui_gesture_cancellation|canvas_input_bounds"
```

These require no display or OpenGL context. They establish the routing contract;
toolkit-specific focus and widget behavior need integration checks when UI is added.
