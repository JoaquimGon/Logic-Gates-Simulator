# Editor Actions

Use `EditorActions` (`src/Editor/Actions/`) for structural edits and editable
properties. Keyboard input and application startup already use this service;
future palettes, inspectors, and loaders should submit the same typed operations.
Component catalogs and configurable-property schemas remain separate work.

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

Supported operations create native gates/inputs/clocks/latches, move/delete
components, configure layouts/input states/clock properties, and add/delete wires
or individual segments. Creation returns component IDs. Creation defaults to
`RejectOverlap`; shortcuts use bounded, deterministic `FindFree` placement.
Compatibility Scene creation methods explicitly allow overlaps, retaining direct
pin-contact behavior; new callers should choose their placement policy deliberately.
The existing origin/pin overlap test remains in use; body-overlap improvements
are tracked separately by TD-G2.

Invalid wire paths, stale segment endpoints, invalid layouts, and unsupported
arity produce typed failures. Feedback geometry remains committed and editable,
with Scene's existing rejection diagnostics and explicit simulation pause. An
accepted edit does not guarantee a simulatable DAG; inspect Scene diagnostics.

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
rerouting and versioned definition/interface migration remain RM-F5/RM-C1 work.

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

CTest runs 18 groups, including five `EditorActionsTests` groups for atomic batches,
preview ownership, configuration/migration, wire surgery/rejection recovery, and
snapshot restoration. `InputTests` additionally verifies actual keyboard spawning,
drag commit/cancellation, scene switching, and middle-segment deletion:

```sh
cmake --build --preset debug
ctest --preset debug
ctest --test-dir out/build/debug -R "editor_|drag_|spawn_|wire_segment_deletion" --output-on-failure
```
