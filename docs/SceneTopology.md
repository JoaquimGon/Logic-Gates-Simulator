# Scene geometry and connectivity

Scene owns committed views, wire geometry, Circuit, and derived nets/indexes.
`Scene::rebuildNets()` validates logical/visual pin counts, discards presentation
previews, and coordinates two independent steps. Rebuilds happen on structural
edits, rather than simulation frames; existing EditorActions batching is preserved.

## Geometry normalization and queries

`Geometry/GeometryTypes.h` describes component bounds and absolute indexed pin
anchors without ComponentView, Scene, GLM, GLFW, or OpenGL dependencies. Scene
adapts committed views to this data; move previews never supply connectivity.
The origin is the snapped electrical reference; center/width/height describe the
floating-point visible body bounds and need not be symmetrical about that origin.

`normalizeWires(wires, pins, nextWireId)` mutates routes in place: it removes
degenerate paths, merges overlapping segments, splits at pin/branch anchors,
and heals degree-two seams without pins. Distinct bent routes are compared by
their complete paths, including reverse orientation, rather than endpoints alone.
Interior crossings without an endpoint or pin remain electrically separate.

Unchanged routes retain their IDs. Splitting/merging retires original IDs and
allocates monotonic replacement IDs; `WireChanges` reports final removed/added
identities. Borrowed wire/path references can expire. The service does not build
nets or update signal state; callers must rebuild derived topology afterward.

GeometryQueries supplies read-only hit, placement, and junction queries. Hits
prioritize pins, wire endpoints/junctions, wire interiors, then visible body bounds.
Placement rejects body-interior intersection, identical origins, and coincident
pin anchors; edges/corners may touch within a `1e-6` world-unit tolerance.
Wires and pin-to-body contact impose no additional clearance. Explicit legacy
`AllowOverlap` creation remains available. Bounds include inversion bubbles and
decorative XOR arcs, while empty shader margins impose no placement clearance.
Selection-outline padding remains visual only; see [rendering](Rendering.md).

Junction queries return integer positions and typed signal state. Scene currently
adapts them to the renderer's packed `glm::vec3` interface; RM-A8 remains pending.

## Connectivity building

`buildConnectivity(wires, pins, circuit, nextNetId)` reads normalized routes and
pin interfaces. It groups shared endpoints, collects drivers/sinks, distinguishes
input/output index spaces, and retains source and destination pin indices.
Its result contains nets, pin/wire indexes, rejected edges, evaluation status,
and the advanced net allocator. Net IDs are regenerated on every rebuild.

The builder replaces Circuit connections without moving or splitting routes.
Feedback edges are accepted; Circuit handles bounded settling independently of
route building (see [simulation](Simulation.md)). Runtime non-convergence uses
`SimulationResult::NON_CONVERGENT`; rejected edges use `CONNECTION_REJECTED`.
Any rejected edge clears the entire partial graph. Scene publishes diagnostics,
suppresses signals, and pauses simulation/automatic clocks until a repaired
rebuild succeeds. Shorted nets retain their existing separate policy: they have
no unambiguous driver, while unrelated valid nets can still run.

Tests cover route preservation in both orientations/insertion orders, pins along
both bent routes, normalization identity changes/idempotence, protected seams,
hit priority, junction state/exclusion, body placement/resize rollback, indexed
outputs, rejection/recovery, and disconnected crossing interiors. Existing input,
snapshot, cancellation, and simulation tests remain part of the same runners.

## Wire routing on movement

`Geometry/WireRouting` uses padded body/pin bounds and orthogonal paths. It first
tries the two simple bends, then a bounded A* search (20,000 visited/expanded
points) with a small turn penalty. Endpoint pins may leave their own padding via
a short outward access segment; other components keep one grid cell of clearance.
Unrelated nets' wire segments are blocked to prevent accidental joining; an
explicit wire endpoint may join its target net. Old paths of moving wires are
excluded while computing replacements, then each completed path is reserved. Routes in
the same net may share segments and are normalized by the existing geometry code.

Default `MoveComponent` operations follow the old pin endpoints to their new
positions (component ID, direction, pin index). Normalization already splits at
pins and branch junctions, so fixed branch endpoints stay in place. All rerouted
paths belong to the same candidate scene/change record as the component move;
failed placement/routing publishes nothing and successful movement rebuilds once.
Rerouting recalculates affected sections, so their manual bends can change.

`MoveComponent{id, position, false}` leaves routes fixed. Input uses this while
Alt is held at mouse release. The usual rebuild reconnects matching pin/wire
geometry, including a drop back in the original position. Simulation connectivity
and saved data remain committed during the drag preview; changes publish on drop.
New wire gestures preview the same router. A blocked endpoint produces no wire
and a brief palette-footer hint. Deleting components leaves dangling wiring;
resizing/arity/definition migrations and live movement-route previews remain future work.

`wire_routing_clearance`, `wire_routing_moves`, `wire_routing_rollback`, and
`wire_alt_drag` cover padding/access, unrelated wires, branches, multiple outputs,
atomic routing failure, restore, both Alt keys, release-time modifiers, same/new
wire reconnection, and powered-route/fixed-wire framebuffer previews.
