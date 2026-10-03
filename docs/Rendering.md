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
tint directly; boxes/latches multiply their slate/border material by it.
Contours/inversion describe the matching shader's silhouette and attachment
policy; they do not change simulation behavior.

Bodies are ordered by ascending stable component ID. Only adjacent instances
with the same shader are batched. Grouping all equal shaders would reorder
translucent overlaps. Unique IDs are required, and actual GPU submissions count
as draw calls; empty batches count zero.

`drawCanvas()` owns the back-to-front order: grid, pin leads, bodies, wires,
junctions, component/segment highlights, cursor marker, pins, and labels.
Engine selects highlight values and active-wire state; it no longer chooses
pass order. Screen overlays and future UI text are submitted afterward.
`CanvasFrame` borrows its spans/wire map only during synchronous drawing. Keep
the adapted components, junction vector, and optional active wire alive until
the call returns. Renderer retains no scene data.

## Body bounds, pin anchors, and leads

Dimensions are positive finite world-space bounds. Native silhouettes retain
their normalized SDFs and scale affinely with each instance's width/height;
aspect changes also stretch internal glyphs and inversion bubbles. Their solid
extents remain inside those bounds. Layout/arity changes preserve electrical
grid anchors; resize does not silently relocate them or attached wires.

`PresentationGeometry` describes native/box silhouette contacts, excluding
decorative XOR arcs and inner source glyphs. Automatic leads attach gates
horizontally; boxes/source symbols follow the nearest side. If an extreme row
misses the silhouette, a bounded contact search and orthogonal dogleg reach an
inner row. Interior anchors need no visible lead.

A nonempty `PinDefinition::lead` contains 2–256 orthogonal, nonzero relative grid
points ending at that pin's anchor. The first point is connected to the body;
the declared route follows afterward. Factory/configuration/movement validate
all absolute points against integer overflow. Views, edits, previews, and
snapshots preserve these paths. Arity generation translates retained default lead
routes along with their generated anchors. Leads are visual stubs, not electrical wires:
they create no nets, junctions, hit targets, or placement clearance. Connectivity
continues to use declared pin anchors and actual wire routes.

CPU contact equations and their matching GLSL silhouettes must change together.
The framebuffer regression checks this boundary for all eleven built-ins and
multiple arities/aspects. Shared GLSL helpers/hot-reload dependency tracking are
still separate work (RM-R1).

## Text and future UI integration

`layoutComponentLabels()` fits body and pin labels using font metrics and body
bounds. Labels follow physical pin sides rather than assuming every input is
left and every output right. Long text shrinks to its available region; nearby
rows constrain pin label height, and body labels prefer a free row.

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
No additional GPU resources or UI dependency are required. The read-only
information popup uses the same rectangles/text. It stores a component ID and
reads the current committed view's name/label and synchronized PinUI states;
deleted components produce no popup. Its screen-space bounds stay inside the
window and overflowing rows scroll. Palette text is submitted before the popup
background so the popup fully covers it in narrow windows.

GPU owners cannot be copied. Initialize, reinitialize, and shut down with their
OpenGL context current. Renderer initialization returns failure for missing
required shaders/font resources; Engine aborts initialization and releases them.
Shutdown is idempotent.

UI-first capture/focus lives in Input; see [InputRouting.md](InputRouting.md).
Shared camera/viewport transforms, DPI resolution, and canvas clipping are complete
under RM-U2; see [CanvasCamera.md](CanvasCamera.md). Renderer uses one forward
matrix for world passes and the same inverse for the procedural grid. Screen text
remains full-window. Explicit instance label/inversion edits are available (RM-C3).
Basic right-click information is complete (RM-F7); truth tables and editing
controls remain RM-U3. Presentation caching remains RM-R3 and packed junction integration remains RM-A8.

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
for visual review. The application build has 44 groups; the headless build has
43. Rendering was exercised on Windows with OpenGL 3.3/NVIDIA; other drivers and
font choices have not been visually verified.

Camera framebuffer regressions also cover a panned/zoomed offset canvas at simulated
2x DPI, every world layer's clipping, and screen text independent of camera changes.
`canvas-viewport.ppm` and `canvas-with-overlay.ppm` are additional review artifacts.
Palette framebuffer checks verify panel/button colors and full-window rendering;
`component-palette.ppm` and `component-palette-drag.ppm` show all native components
and the placement outline/badge. Both previews were visually reviewed. Input
regressions cover all catalog buttons, overlap rejection, cancellation, scrolling,
and mode isolation. Visible desktop interaction remains a manual integration check.
`component-information.ppm` shows named latch inputs and its two independent
outputs in a right-click popup. Framebuffer checks verify its opaque background
and text; the preview was visually reviewed.
