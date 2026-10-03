# Editor Actions

Use `EditorActions` (`src/Editor/Actions/`) for structural edits and editable
properties. Keyboard input, the component palette, and application startup use
this service; future inspectors and loaders should submit the same typed operations.
Definitions/catalog creation are described in [ComponentDefinitions.md](ComponentDefinitions.md).
Instance customization uses explicit fields in ConfigureComponentProperties;
the read-only information popup shows live pins; further educational controls remain pending.

## Applying a complete edit

`apply(EditBatch)` stages operations in an isolated scene. Every operation must be
valid; placement and removed-pin checks inspect the final candidate. Success
publishes one change and rebuilds normalized geometry/nets once for structural
edits. Input state and clock-property edits do not rebuild topology. Failed batches
leave geometry, runtime state, allocation counters, and revision unchanged.
Empty batches and unchanged properties/positions produce no change record.

```cpp
EditorActions actions(scene);
auto result = actions.apply({
    MoveComponent{firstId, {10, 0}},
    MoveComponent{secondId, {0, 0}},
    DeleteWireSegment{wireId, {4, 0}, {4, 4}}
});
if (!result) {
    // Present result.message; result.error is a typed EditError.
}
```

`CreateComponent` selects a definition ID/version and typed instance options;
`RegisterComponentDefinition` can atomically register a validated custom box type
before creating it in the same batch. Snapshots share immutable catalog storage;
definition-only edits rebuild no topology. Native creation requests remain thin
compatibility adapters to the same factory.

Supported operations create native gates/inputs/clocks/latches, move/delete
components, configure layouts/input states/clock properties, and add/delete wires
or individual segments. Creation returns component IDs. Creation defaults to
`RejectOverlap`; shortcuts use bounded, deterministic `FindFree` placement.
Compatibility Scene creation methods explicitly allow overlaps, retaining direct
pin-contact behavior; new callers should choose their placement policy deliberately.
Placement also checks full rectangular body interiors. Edges/corners may touch
(with a small float tolerance); identical origins and coincident pin anchors remain
invalid. Wires and pin-to-body contacts impose no additional clearance. The same
check applies to creation, movement, configuration, and future import batches.

Invalid wire paths, stale segment endpoints, invalid layouts, and unsupported
arity produce typed failures. Feedback is accepted and remains editable. An accepted
edit need not settle: inspect `Scene::getLastEvalResult()` for `NON_CONVERGENT`,
which pauses automatic simulation until an input or edit requests another attempt.
Invalid connections retain separate rejection diagnostics. See [simulation](Simulation.md).

## Preview, commit, and cancel

`beginMove(id)` returns a token. `previewMove(token, position)` updates only
presentation views. Committed positions, wire routes, nets, and simulation remain
unchanged. `getComponentViewMap()`/`getComponentView()` include previews;
`getCommittedComponentView()` reads model placement.

`commitMove(token)` validates and commits the final position once; failure
discards the preview. `cancelMove(token)` performs no topology rebuild or history
operation. Tokens protect against stale gestures cancelling a newer preview.
Other edits/restores are rejected while a preview owns the scene. Input cancels
its preview before switching scenes; keep the previous Scene alive until then.

## Configuration and pin migration

`ConfigureComponentProperties` accepts optional `label`, `inputCount`,
`inverted`, `clockFrequency`, and `clockPaused`. Omitted fields retain their
current values; an empty label clears body text. No property reflection or
serialized override system is involved.

~~~cpp
auto result = EditorActions(scene).apply({
    ConfigureComponentProperties{
        .componentId = gateId,
        .label = "Carry",
        .inputCount = 4,
        .inverted = true
    }
});
~~~

Scalable gate input counts clamp to 2–8. NOT accepts only one input; fixed custom
interfaces reject count changes. Existing low-level creation/layout APIs retain
their wider validated range. Arity edits reuse current geometry, preserve output
anchors and surviving pin identities/labels/leads, regenerate input rows, and grow
body height when needed. Body dimensions are retained when shrinking. Wire routes
stay fixed. Removing a wired input requires deleting its wire in the same batch;
the explicit layout API below still offers its existing removed-pin policies.

Inversion selects AND/NAND, OR/NOR, or XOR/NXOR behavior and the matching native
shader/bubble together. Pin positions, body size, tint, labels, and definition
identity stay intact. The identity refers to the creation/default definition;
`Gate::getType()` and `isInverted()` report current behavior. NOT has intrinsic
inversion; custom boxes do not support this visual toggle.

Clock fields apply only to clocks; frequency must be finite and at least 0.1 Hz.
Partial edits preserve the other clock setting and accumulated phase. Labels,
inversion, and clock edits rebuild no topology. Unsupported fields, overlap, and
wired-pin removal reject the complete batch. Snapshots retain all edited values.
The read-only right-click popup shows names and live pin states. Inspector edit
controls and truth-table presentation remain RM-U3.

`ConfigureComponent` receives a complete layout, with logical pin identity carried
by direction and index, independently of vector order. Gate input counts change
together with their visual interface: NOT has one input, other gates require two
or more. Other native components keep their behavior's fixed counts.

Surviving indices keep identity and retained output/runtime state. Added inputs
start false; derived input values are recomputed through connectivity/propagation.
Removing an attached input defaults to `RejectAttached`: remove its wires in the
same batch, or explicitly select `LeaveWires`. That policy leaves routes in place
as dangling geometry and prevents retained pins reusing the removed attachment.
Moved/resized pin anchors attach by their final grid coordinates. Automatic wire
rerouting and versioned definition/interface migration remain RM-F5/RM-C5 work.

View pin lists and committed wires are read-only to callers; placement setters
and pin editing are internal to Scene/action implementation. Runtime interactions
such as toggling an input or stepping a clock remain simulation operations.

## Reversible changes and lifetime

`EditResult::change` contains complete before/after scene images and final added/
removed wire IDs. Normalization may replace `insertedWireIds`; they are provisional
compatibility IDs and must not be used as a history delta. Store whole images.
`restore(image, expectedRevision)` rebuilds derived topology once and preserves
monotonic allocation counters, preventing identity reuse. It restores retained
latch/source state and clock phase; caches are rebuilt and need normal propagation.

Scene revisions track committed editor edits/rebuilds, not individual simulation
ticks. A restore deliberately rewinds saved runtime state. Undo/redo controls,
history retention limits, and persistence policy remain RM-I1/RM-F1 work.

Cache IDs and reacquire borrowed objects after edits/restores or preview lifecycle
transitions. Publishing replaces model/view objects, invalidating raw pointers and
references. Custom native classes must implement `clone()` to copy all retained
behavior/presentation state. Current transactions copy complete scenes and images;
memory/time scale with scene size, and callers control record retention.

## Verification

CTest runs 44 groups with the application (43 headlessly), including seven `EditorActionsTests` groups for atomic batches,
preview ownership, configuration/migration, wire surgery/rejection recovery,
snapshot restoration, body-placement rollback, and explicit component settings
(including native-pair truth tables, clock phase, and custom lead preservation). `InputTests` additionally verifies actual keyboard spawning,
drag commit/cancellation, scene switching, and middle-segment deletion:

```sh
cmake --build --preset debug
ctest --preset debug
ctest --test-dir out/build/debug -R "editor_|drag_|spawn_|wire_segment_deletion" --output-on-failure
```

## Gesture integration and modes

GLFW events are adapted by Input and dispatched to the concrete handlers in
`Editor/Gestures/`: DragGesture owns the move token, WireGesture owns provisional
routing/deferred branching, PanGesture updates camera offsets, and Selection owns
selected identities and deletion requests. Gesture handlers do not depend on GLFW.
Creation shortcuts in `Editor/ComponentShortcuts.cpp` map keys to catalog IDs and
use `CreateComponent` with `FindFree`; defaults live in NativeDefinitions.cpp.

The left palette (`UI`) uses catalog IDs too. Pressing a button captures input;
dragging draws an outline without changing the scene. Releasing inside the canvas
submits one `CreateComponent` with overlap rejection. Failed or cancelled drops
create no edit record. The palette releases capture before applying the batch,
and consumes the release so it cannot also begin a canvas gesture.

Selection mode edits layout and routes without operating component bodies.
Interaction mode delegates runtime clicks through `Scene::handleClick()`, with no
fallback to dragging; creation, deletion, and wiring are blocked. Pan/zoom and
simulation work in either mode. F2 toggles modes once per press. Switching mode
cancels the active gesture immediately, before subsequent mouse-release callbacks;
focus loss and scene changes likewise discard unfinished gestures and queued keys.
UI mode controls can call `Input::setMode()`. UI-first event routing and capture
are implemented; see [InputRouting.md](InputRouting.md). Capture/focus transfers
cancel previews while retaining selection. Publish ownership before applying a UI
edit so active previews do not reject it. Shared picking/rendering transforms and DPI-aware clipping are complete under RM-U2;
see [CanvasCamera.md](CanvasCamera.md).
