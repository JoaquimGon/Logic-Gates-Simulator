# Rendering and presentation boundaries

## Ownership and data flow

`NativeDefinitions.cpp` declares resources, body contours, tints, and label policy.
`ComponentFactory` resolves that metadata into views. Neither the renderer nor
the text layout inspects logical gate types or concrete latch/source classes.

`buildComponentPresentation()` adapts the common view interface once per frame,
including movement previews. Its returned values own their pin positions, labels,
and lead geometry. They contain no simulation pointers. `ComponentRenderData`
and `CanvasFrame` form the graphics-facing contract; the adapter is the only part
of this path that reads views.

`Renderer` coordinates draw passes and owns meshes and shader programs.
`TextPainter` owns the font texture/text mesh and submits already laid-out runs.
`LabelLayout`, `OverlayPresentation`, instance batching, and `TextGeometry` are
CPU-only functions in `simulator_presentation`; they build and test without
OpenGL, GLAD, a font file, or a display. Baked font metrics are separate from GPU
atlas ownership. Overlay formatting depends on neutral simulation status values,
not Circuit implementation.

## Instances, batches, and layers

The component vertex shader receives position(2), size(2), and tint(4) per
instance. There is no batch-wide size uniform. Native solid shapes use declared
tint for their fill; boxes/latches multiply their slate fill by it. All use a
neutral slate outline matching the output bulb's unpowered fill color.
Contours/inversion describe the matching shader's silhouette and attachment
policy; they do not change simulation behavior.

Gates, inversion bubbles, inputs, clocks, boxes/latches, and pin/junction markers
share the output bulb's unpowered fill color `(0.12, 0.15, 0.20)`. Fragment derivatives keep
the inset stroke approximately 1.5 framebuffer pixels wide at 1x zoom and above,
independent of instance size/aspect. Below 1x, thickness scales with zoom
(`1.5 * zoom` pixels), retaining pixel anti-aliasing. Screen-space card previews
always use the 1.5-pixel thickness, independent of the canvas camera. Tiny point sprites clamp their rim width
to retain a colored core. Inversion bubbles retain their complete circular rim
at the body join; XOR/NXOR rear arcs retain their original colored width with a
matching rim added outward, clipped by the gate quad. Their rim follows the same zoom rule;
a stable circle gradient avoids derivative artifacts along their centerline.
Input/clock outlines follow the outer body rather than bordering their inner
glyph cutouts. The output bulb retains its distinct material.

Body outlines are inset shading. Rear arc rims extend outward within the shader
quad; neither changes component sizing or electrical pin anchors.
AND's straight body and cap use a continuous union distance field on both CPU
and GPU, preventing their internal join from being shaded as a false edge.
Outline helpers remain small local shader functions; shader helper consolidation
and dependency-aware includes remain RM-R1.

Bodies are ordered by ascending stable component ID. Only adjacent instances
with the same shader are batched. Grouping all equal shaders would reorder
translucent overlaps. Unique IDs are required, and actual GPU submissions count
as draw calls; empty batches count zero.

`drawCanvas()` owns the back-to-front order: grid, pin leads, bodies, wires,
junctions, component/segment highlights, pins, wire-start/cursor markers, and labels.
Engine selects highlight values and active-wire state; it no longer chooses
pass order. Screen overlays and future UI text are submitted afterward.
`CanvasFrame` borrows its spans/wire map only during synchronous drawing. Keep
the adapted components, junction vector, and optional active wire alive until
the call returns. Renderer retains no scene data.

## Body bounds, pin anchors, and leads

Dimensions are positive finite shader scales in world units, without grid-cell
rounding or an even-cell requirement. Native silhouettes retain
their normalized SDFs and scale affinely with each instance's width/height;
aspect changes also stretch internal glyphs and inversion bubbles. Their solid
extents remain inside those bounds. Layout/arity changes preserve electrical
grid anchors; resize does not silently relocate them or attached wires.

`BodyBounds` stores floating-point left/bottom/right/top extents independently of
the grid origin. `normalizedBodyBounds()` describes the visible SDF, including
inversion bubbles and decorative XOR arcs; `bodyBounds()` applies the instance
scale and origin. `ComponentView` and `ComponentBodyInstance` expose the same
derived bounds. Picking and placement use their rectangular body interiors,
while pin anchors remain integer grid coordinates. The body center may differ
from the origin, particularly for NOT and inverted gates. Previews fit and center
these visible bounds rather than the shader quad's empty margins.

Selection/hover and wire-segment outlines reuse the wire shader with triangle
strips around a slightly rounded rectangle. The stroke is 0.006 world units,
half the electrical wire's 0.012 width; corners use a 0.015 radius, clamped for
small bodies. `BodyHighlight` and the renderer accept explicit bounds. Outline
padding is visual only and does not affect picking, placement, or connectivity.

Wire guides use a 0.030-world-unit diameter, with 0.55 opacity for idle hover,
0.9 for the fixed starting point, and 1.0 for the moving endpoint. Committed
junctions use a 0.034 diameter. Guides draw above pins so a pin cannot obscure
the creation marker. The start marker appears immediately on branch press and
stays at the origin throughout dragging. Release clears creation markers;
after a successful commit, cursor/pin highlighting resumes once the pointer
leaves the endpoint's grid cell. Cancellation releases the marker immediately.

`PresentationGeometry` describes native/box silhouette contacts, excluding
decorative XOR arcs and inner source glyphs. Automatic leads attach gates
horizontally; boxes/source symbols follow the nearest side. If an extreme row
misses the silhouette, a bounded contact search and orthogonal dogleg reach an
inner row. Interior anchors need no visible lead.

A nonempty `PinDefinition::lead` contains 2â€“256 orthogonal, nonzero relative grid
points ending at that pin's anchor. The first point is connected to the body;
the declared route follows afterward. Factory/configuration/movement validate
all absolute points against integer overflow. Views, edits, previews, and
snapshots preserve these paths. Arity generation translates retained default lead
routes along with their generated anchors. Leads are visual stubs, not electrical wires:
they create no nets, junctions, hit targets, or placement clearance. Connectivity
continues to use declared pin anchors and actual wire routes.

CPU contact/bounds equations and their matching GLSL silhouettes must change together.
The framebuffer regression checks this boundary for all twelve built-ins and
multiple arities/aspects. Shared GLSL helpers/hot-reload dependency tracking are
still separate work (RM-R1).

## Text and future UI integration

`layoutComponentLabels()` fits body and pin labels using font metrics and body
bounds. Labels follow physical pin sides rather than assuming every input is
left and every output right. Long text shrinks to its available region; nearby
rows constrain pin label height, and body labels prefer a free row.
Input/output names are fitted beneath their symbols instead of covering them.
The output bulb's normalized radius is 0.32 in both CPU contact geometry and its
shader. Its tint comes from synchronized incoming pin state: green for HIGH,
dark for LOW, and gray for unavailable signals. Presentation needs no logic pointer.

`layoutDebugOverlay()` produces right-aligned screen runs. Feedback/rejection
warnings remain visible with F3 metrics disabled. `TextPainter` shares glyph
generation and upload for world coordinates (Y up) and screen pixels (Y down).
For another text presentation, produce `TextRun` values and call
`Renderer::drawText(runs, TextSpace::Screen)` after the canvas pass.

The component palette is one small `UI` class under `src/UI/`. `UI.cpp` handles
layout and input; `UIDrawing.cpp` draws rectangular buttons, labels, and a drag
badge. `Renderer::drawScreenRect()` reuses the wire shader/mesh for a colored
screen rectangle. Both rectangles and text use the full window after the clipped,
offset canvas pass. The placement outline uses the same camera as the canvas.
No additional GPU resources or UI dependency are required. The information
popup and its input/output name field use the same rectangles/text. It stores a component ID and
reads the current committed view's name/label and synchronized PinUI states;
deleted components produce no popup. Its screen-space bounds stay inside the
window and overflowing rows scroll. Palette text is submitted before the popup
background so the popup fully covers it in narrow windows.
The name field captures typing only in Selection mode and submits the existing
label action on Enter. Names are limited to 32 printable ASCII characters to
match the current font atlas; other inspection data remains read-only.

GPU owners cannot be copied. Initialize, reinitialize, and shut down with their
OpenGL context current. Renderer initialization returns failure for missing
required shaders/font resources; Engine aborts initialization and releases them.
Shutdown is idempotent.

UI-first capture/focus lives in Input; see [InputRouting.md](InputRouting.md).
Shared camera/viewport transforms, DPI resolution, and canvas clipping are complete
under RM-U2; see [CanvasCamera.md](CanvasCamera.md). Renderer uses one forward
matrix for world passes and the same inverse for the procedural grid. Screen text
remains full-window. Explicit instance label/inversion edits are available (RM-C3).
Basic right-click information and input/output naming are complete (RM-F7/RM-F8);
truth tables and other editing controls remain RM-U3. Presentation caching remains RM-R3 and packed junction integration remains RM-A8.

## Verification

`PresentationTests` adds three headless groups covering mixed sizes/order,
preview isolation, leads through 255-input interfaces, lead edits/rollback,
world/screen glyph geometry, fitted labels, and warnings without metrics.
`RenderTests` uses an invisible GLFW/OpenGL 3.3 window and framebuffer to verify
mixed sizes, tints, stable compositing, native contacts, edited inversion bubbles/leads, draw counts, and text submission. It runs only with the application build and skips (code 77) when a
context is unavailable; skips do not establish pixel correctness.

```sh
ctest --preset x64-debug -R "render_|component_pin_leads|text_presentation"
```

The framebuffer test writes `mixed-sizes.ppm` and
`component-presentations.ppm` under the build's `render-artifacts/` directory
for visual review. The application build has 46 groups; the headless build has
45. Rendering was exercised on Windows with OpenGL 3.3/NVIDIA; other drivers and
font choices have not been visually verified.

Camera framebuffer regressions also cover a panned/zoomed offset canvas at simulated
2x DPI, every world layer's clipping, and screen text independent of camera changes.
`canvas-viewport.ppm` and `canvas-with-overlay.ppm` are additional review artifacts.
Palette framebuffer checks verify panel/card colors and full-window rendering;
`component-palette.ppm` and `component-palette-drag.ppm` show all native components
and the placement outline/badge. Both previews were visually reviewed. Input
regressions cover all catalog buttons, overlap rejection, cancellation, scrolling,
and mode isolation. Visible desktop interaction remains a manual integration check.
`component-information.ppm` shows named latch inputs and its two independent
outputs in a right-click popup. Framebuffer checks verify its opaque background
and text; the preview was visually reviewed.
`output-bulb-off.ppm`, `output-bulb-on.ppm`, `output-bulb-unavailable.ppm`, and
`input-output-naming.ppm` verify bulb colors, fitted names, and the active name
field. All four previews were visually reviewed.

## Component palette previews

The small UI class draws Native/Custom tabs with the existing rectangle/text
primitives. Native cards fit each definition's default aspect ratio and shader
inside a preview region, with the display name below. Latches retain their central
body label and omit pin labels. The declaration table supplies native ordering.
Custom entries are full-width rows with display names and input/output counts;
the catalog starts with no custom definitions.

Scrolling draws partially visible cards/rows with a screen clip around the list,
rather than hiding whole items. Hit testing uses the same list bounds. Headers,
tabs, footer and popups draw after releasing the clip; framebuffer scaling uses
the existing viewport conversion. `component-palette-scrolled.ppm` verifies the
bottom scroll position at 2x DPI, including partial cards and header/footer isolation.

`Renderer::drawScreenComponent()` reuses the component mesh, instance packing,
and loaded shaders with a screen projection. Logical window pixels keep previews
independent of canvas pan/zoom and preserve framebuffer/DPI scaling. Previewing
does not instantiate logical components or mutate the scene.

The existing framebuffer runner additionally checks native preview color,
camera independence, and 2x DPI. `component-palette-dpi.ppm`,
`component-palette-custom-empty.ppm`, and `component-palette-custom-list.ppm`
provide review artifacts. The populated custom list is a test-only fixture;
no sample custom definitions are registered by the app.

Bounds regressions also verify fractional sizes (`0.173 x 0.137`), grid movement
and snapshot restoration, unchanged electrical anchors, empty-margin picking,
body-overlap rollback and exact boundary contact. Pixel checks measure native
card centering, including bubbles, and outline edges/corners/interiors.
`rounded-component-bounds.ppm` is the outline review artifact. All 46 Debug
and 45 headless AddressSanitizer groups pass; outline, palette-centering, and
component-presentation previews were visually reviewed.

Component-outline checks cover perimeter shading, constant pixel thickness at
1x/2x zoom, tapering below 1x, colored XOR/NXOR rear arcs, full NAND/NOR/NXOR bubble rims and colored interiors, seam-free AND
fills, and outline-aware card centering. `component-outline-nand.ppm`, native
palette, and component-presentation previews were visually reviewed. All 46
Debug checks and 45 headless AddressSanitizer checks pass.

Wire-marker regressions cover immediate/deferred starts, fixed origins during
dragging, release/cancellation cleanup, and same-cell endpoint suppression.
`wire-drag-markers.ppm`, `wire-hover-marker.ppm`, and `wire-markers-cleared.ppm`
check marker size/opacity, layering above pins, and clean removal after drawing.
All three framebuffer previews were visually reviewed; the 46 Debug and 45
headless AddressSanitizer groups pass.

## Circuit viewpoints

`Editor/CircuitViews` owns a small vector of named, independent scenes and their
last pan/zoom values. Main exists from startup; creation adds an empty scene.
Switching reuses `Input::setScene()` to cancel previews, clear selection and
queued shortcuts, then restores the destination camera. Existing EditorActions
and scene snapshots continue to operate on one scene. Only the active scene is
simulated; inactive clock phases/signals are retained without wall-time catch-up.

`UI/CircuitTabs` provides the top strip and its rename popup using existing
Renderer rectangles/text. The shared canvas viewport excludes its 30-pixel
header. Each tab measures `unnamed` at 0.42 font scale with 10-pixel side
padding and a 24-pixel close-button slot; longer names replace the last fitting
characters with `...`. Main remains pinned while the other tabs scroll. A separate square plus button
follows the last circuit tab; creation and resizing reveal it with the newest
active tab. Other active tabs are revealed when switching.

A one-second hover or right-click opens a popup with the full name and a
prefilled field. Clicking the field selects its contents; Enter saves, Backspace
and Ctrl+A edit, and Escape/outside click/focus loss/resize discard drafts.
Keyboard capture blocks canvas shortcuts while typing. Names are metadata,
limited by the field to 64 printable ASCII characters; blank names use the
`unnamed` default (or Main for the main tab).

Non-main tabs show an X at their right edge. It opens a Delete/Cancel popup
that captures pointer and keyboard input until confirmed or dismissed. Escape,
outside clicks, focus loss, and resize cancel it. Active deletion switches to
the left neighbor before destroying the scene; deleting an inactive tab preserves
the current scene and camera. Main is protected in both the UI and model.
There are no saved-state checks. Subcircuit I/O panels, validation, packaging, nesting, and persistence
remain separate future work.

`CircuitViewsTests` checks independent contents/signals/cameras, scene-local
snapshot restoration, cancelled gestures, header drop cancellation, hover timing,
renaming/capture/cancellation, overflow scrolling, resize visibility, and active/
inactive deletion with Main protection and safe input/scene lifetime.
Framebuffer checks draw the active scene, tab titles, and rename/delete popups at
normal and 2x DPI; review artifacts are `circuit-tabs.ppm`,
`circuit-tab-name.ppm`, `circuit-tab-delete.ppm`, and `circuit-tabs-2x.ppm`.
All 47 Debug and 46 headless AddressSanitizer checks pass; the tab and popup
previews were visually reviewed.
