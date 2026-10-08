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

`layoutDebugOverlay()` produces right-aligned screen runs. Diagnostics draw
after all UI, including popups, on a padded black background with 50% opacity.
They use the full window regardless of canvas/UI clipping. Feedback/rejection
warnings remain visible with F3 metrics disabled; hiding healthy diagnostics
also hides their background. `TextPainter` shares glyph
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
last pan/zoom values. Main exists from startup as a permanent Workspace. Each viewpoint stores an
explicit `CircuitViews::Role` (Workspace or Subcircuit); creation adds an empty
scene with the chosen role. Role changes preserve the scene, camera, signals,
and existing names. Enabling subcircuit editing assigns defaults to unnamed
inputs, outputs, and clocks. Main rejects the Subcircuit role. Names and port components do not
infer authoring intent; the role is separate from component behavior.
Switching reuses `Input::setScene()` to cancel previews, clear selection and
queued shortcuts, then restores the destination camera. Existing EditorActions
and scene snapshots continue to operate on one scene. Only the active scene is
simulated; inactive clock phases/signals are retained without wall-time catch-up.

`UI/CircuitTabs` provides the top strip and its rename popup using existing
Renderer rectangles/text. The shared canvas viewport excludes the 30-pixel
application bar and the 30-pixel circuit header below it. Each tab measures `unnamed` at 0.42 font scale with 10-pixel side
padding and a 24-pixel close-button slot; longer names replace the last fitting
characters with `...`. Main remains pinned while the other tabs scroll. A separate square plus button
follows the last circuit tab and opens New workspace/New subcircuit choices.
Escape, outside clicks, focus loss, and resize discard the menu without creating
a scene. It captures input and suppresses hover popups until dismissed.
Creation and resizing reveal the plus with the newest
active tab. Other active tabs are revealed when switching.

A one-second hover or right-click opens a popup with the full name and a
prefilled field. Clicking the field selects its contents; Enter saves, Backspace
and Ctrl+A edit, and Escape/outside click/focus loss/resize discard drafts.
Keyboard capture blocks canvas shortcuts while typing. Names are metadata,
limited by the field to 64 printable ASCII characters; blank names use the
`unnamed` default (or Main for the main tab). The popup displays the role and
offers Convert to subcircuit/Use as workspace on non-main views. These change
metadata only; an unfinished rename draft is discarded when changing purpose.
Subcircuit editors have a small SUB badge without widening the compact tabs.
The Subcircuit bottom tab shows the authoring interface overview. Valid saves
publish boxed components in Custom; placed boxes offer Edit subcircuit in their
right-click popup. See [Subcircuits.md](Subcircuits.md).

Non-main tabs show an X at their right edge. It opens a Delete/Cancel popup
that captures pointer and keyboard input until confirmed or dismissed. Escape,
outside clicks, focus loss, and resize cancel it. Active deletion switches to
the left neighbor before destroying the scene; deleting an inactive tab preserves
the current scene and camera. Main is protected in both the UI and model.
There are no saved-state checks. Main circuit JSON persistence is described in
[CircuitFiles.md](CircuitFiles.md) and [Subcircuits.md](Subcircuits.md).

`CircuitViewsTests` checks independent contents/signals/cameras, scene-local
snapshot restoration, cancelled gestures, header drop cancellation, hover timing,
renaming/capture/cancellation, overflow scrolling, resize visibility, and active/
inactive deletion with Main protection and safe input/scene lifetime.
Framebuffer checks draw the active scene, tab titles, and rename/delete popups at
normal and 2x DPI; review artifacts are `circuit-tabs.ppm`,
`circuit-tab-name.ppm`, `circuit-tab-delete.ppm`, and `circuit-tabs-2x.ppm`.
All 47 Debug and 46 headless AddressSanitizer checks pass; the tab and popup
previews were visually reviewed.

## Application bar

`UI` draws a 30-pixel bar across the window with one File button and a small
dropdown: Save, Save As, Open, Load subcircuit in workspaces; Save (subcircuit)
and Save As (subcircuit) in subcircuit editors. It reuses rectangle/text helpers;
each entry queues a single typed `FileCommand` for Engine to consume before
processing/rendering the next scene frame. Engine owns the current Main file path
while each subcircuit editor owns its own path. App/CircuitFiles handles
file-command execution and the native picker; Persistence handles JSON/file IO.
UI only routes commands
and displays success/errors in the bar. Hover highlights its row. The panel and
circuit tabs start below the bar, and the canvas viewport follows that layout.

The open menu captures pointer and keyboard events, cancelling existing gestures
and dismissing other popups. Escape, an outside click, focus loss, or resizing
closes it. The dismissal click is consumed so it cannot start a canvas edit.
Circuit deletion confirmation keeps priority over the menu. Regression checks
cover menu capture/dismissal, one-shot commands, and Main-only reconstruction;
framebuffer checks cover placement, hover, and normal/2x display scaling.
All 50 Debug checks pass. `file-menu.ppm` and `file-menu-2x.ppm` were visually reviewed.

## Editor mode buttons

Compact Sel. and Int. buttons sit just above the bottom feature panel. Their
backgrounds cover only each button, with no full-width bar. The controls overlay
a small 144-pixel-wide canvas area; the rest remains available for circuit edits.
Their hover help uses the full Selection and Interaction names. Pointer/scroll
events and palette drops on the controls stay in the UI. Buttons use the
existing rectangles/text and Input::setMode; switching mode cancels unfinished
canvas gestures just like F2. The current button is faded and underlined;
the other remains bright and clickable. Hovering the exact button shows a
small informational popup above the row, explaining editing or input/clock
operation. Leaving, focus loss, resize, and other popups hide the tooltip.
F2 and the buttons share the same mode state.

## Bottom panel

`UI` reserves a bottom rectangle aligned to the right of the component palette.
Its height is the smaller of 180 logical pixels or 40% of the available space
below the top bars. The shared canvas viewport ends at its upper edge, keeping
rendering, picking, panning, and palette placement aligned after resize. The
left component panel retains its full height.

The Subcircuit tab is visible only for subcircuit editors; workspaces retain
only the Tab 2 placeholder. Both reuse screen rectangles/text and the existing
active underline. There are no closing/creation controls. The Subcircuit body
shows the full circuit name, basic named-input/output validity, vital-component
counts, and ordered Input/Output/Clock name rows. Header rows remain fixed while
the list scrolls. Switching viewpoints resets its list scroll position. Pointer events and scrolling over
the panel are consumed, and palette drops there are cancelled by canvas bounds.
Floating component/name/File popups retain their normal overlay priority.

Existing layout/input and framebuffer checks cover separation from the palette
and canvas, tab selection without edits/zoom, and normal/2x display scaling.
`bottom-panel.ppm` and `bottom-panel-2x.ppm` were visually reviewed.

Viewpoint-role regressions verify both creation choices, menu capture/cancellation,
Main protection, name-independent intent, conversion without scene mutation,
and preservation through Main loading. Five targeted Debug groups pass;
`circuit-view-create.ppm`, `circuit-view-role.ppm`, and normal/2x role-popup
previews were visually reviewed.

## Subcircuit naming and overview

`Editor/InterfaceComponents` reads committed logical InputPin/OutputPin/Clock
instances, including custom boxes with those native behaviors. It orders rows
by kind and component ID and generates missing labels using the first available
`input N`, `output N`, or `clock N`. Defaults are actual editable instance labels,
not display-only hints. Existing labels are preserved.

Scene carries the explicitly enabled interface-naming policy; CircuitViews
derives its Workspace/Subcircuit role from that policy, avoiding a second role
flag. Conversion fills missing labels through existing editor operations.
EditorActions assigns defaults within creation/edit batches and rejects blank
input/output property edits atomically. Restore retains the destination editing
policy and names missing vital components, so an older snapshot cannot silently
turn a subcircuit into a workspace or restore unnamed ports. Workspace labels
remain optional. Clock names can be edited in subcircuit component popups and
native clock labels render beneath the glyph.

Validity checks named inputs/outputs, unique names per direction, and shorted,
rejected, or non-converging wiring. Save also checks containment and expansion
limits before publishing. See [Subcircuits.md](Subcircuits.md). Name/count/status/list
rows update from the committed scene after creation, deletion, edits, or restore.
All 50 Debug groups and four targeted AddressSanitizer checks pass.
Normal/2x, invalid-interface, and scrolled list previews
are `subcircuit-panel.ppm`, `subcircuit-panel-2x.ppm`,
`subcircuit-panel-invalid.ppm`, and `subcircuit-panel-scrolled.ppm`.

## Saved-state indicator

The top navigation bar draws the active view's **Unsaved changes** message in muted
amber at the far right. Subcircuit editors add **save for Custom**. The message uses
existing text rendering and reserves its width, so File operation status text on
the left cannot overlap it. It neither flashes nor captures input. Saving/loading
clears it; new views and later persisted edits restore it. Normal and 2x framebuffer
checks cover visibility, clearing, and return after edits.

## Group selection overlays

CanvasFrame borrows selected body and wire-segment highlights plus an optional
selection rectangle. While dragging a box, fully enclosed objects immediately show
rounded bounds at 35% opacity; leaving the box removes that preview. Releasing
commits the selection and switches its outlines to the normal solid highlight.
Hovering unselected objects stays at 40% opacity. Candidate IDs are refreshed on
pointer movement using body bounds and wire paths; they do not edit the scene or
history. Cancelling the gesture discards candidates and restores prior highlights.
All overlays are clipped with the canvas camera. The pending rectangle
uses a 6% blue world-space fill and a faint outline. Selected wires render from
Scene's presentation-only translated paths during group previews; persisted paths
and simulation topology remain committed until release. No screen-space widget or
shader framework is involved. Group and rectangle framebuffer previews cover faint
versus opaque outlines and normal/panned/zoomed 2x drawing.

## Shared shaders and reusable buffers

Native shaders include `components/common/sdf.glsl` for their existing distance
functions and `outline.glsl` for outlines and antialiasing. Each native fragment
keeps its own silhouette composition and material; the output bulb and point
markers retain their separate materials. CPU silhouette/contact equations remain
in `Components/Definitions/PresentationGeometry.cpp` and must match GLSL.

Shader loading expands standalone quoted includes relative to the including file.
It tracks roots and nested dependencies, including missing files, and uses GLSL
`#line` source IDs with an ID/path diagnostic map. Hot reload checks every 200 ms,
compares against the last attempted timestamps, and retries after another edit
or a missing file reappears. Failed reads, compilation, or linking keep the valid
program. Uniform locations (including absent uniforms) are cached per program;
only a successfully linked replacement clears that cache.

Mesh keeps allocated vertex/instance capacity, grows it when needed, and uses
`glBufferSubData` for changed contents. Identical packed values skip uploading;
instance attributes are configured again only when their layout changes. Smaller
submissions update their draw count without shrinking storage. Renderer reuses
wire, lead, pin, and junction staging vectors; wire geometry appends directly
without a temporary vector per wire. Leads have their own mesh so wire/UI drawing
cannot overwrite their retained data. Text reuses its glyph staging vector.

The renderer still rebuilds staging from current presentation values. Comparing
complete packed data makes movement, size/arity/lead changes, routing/topology,
signal colors, hover colors, and tints update naturally, without a separate
invalidation/revision system. Camera and shader changes update uniforms; previews
and overlays continue submitting current values. Component body batching retains
stable ID order and groups only adjacent equal shaders with the same rear-arc flag. UI/shared transient meshes
can still upload between different draws; this is deliberately a limited buffer
reuse optimization, not a cache of entire scenes or every UI batch.

`RenderTests --profile` measures 80 warmed drawing frames with 160 bodies (four
pins each) and 160 wires, excluding scene/presentation construction, labels, and
UI. Edited frames change size, lead geometry, pin state, and wire state. On the
local Debug build/OpenGL 3.3 NVIDIA 581.57, the initial before/after run was:

| Drawing pass | Before | After |
| --- | --- | --- |
| Static elapsed time | 527 ms | 425 ms |
| Edited elapsed time | 531 ms | 434 ms |
| Buffer storage replacements, either pass | 320 | 0 |
| Static uploads / uniform lookups | 320 / 800 | 0 / 0 |
| Edited uploads / uniform lookups | 320 / 800 | 318 / 0 |

Timings vary with the machine and do not predict total application frame time.
Tests assert deterministic storage/upload/lookup behavior rather than speed.
Resource regressions cover vertex growth/shrink/empty restoration, instance color
changes, present/missing uniform caching, failed compile/link retention, nested
include reloads, backward timestamps, missing-file recovery, and include cycles.
Existing native/contact/arity, overlay, preview, signal, and DPI framebuffer tests
remain in place. All existing generated framebuffer previews match the baseline
byte for byte; native, NXOR-outline, and palette previews were visually reviewed.

## Logic-gate popup settings

Right-click a logic gate to see its current type, body label, input count,
inversion, and live pin names/states. In Selection mode, the label field uses
Enter to commit and Escape to cancel. Minus/plus changes supported input counts
between 2 and 8; the inversion button switches AND/NAND, OR/NOR, and XOR/NXOR.
NOT reports its fixed one input and fixed inversion without mutable controls.
Registered gate definitions expose count/inversion edits only when their existing
layout/presentation supports them. Clocks and latches gain no gate settings.
Interaction mode keeps the popup informative and disables editing controls.

Each settings click uses `ConfigureComponentProperties` and records one successful
change in the active view's existing undo/redo history. Native gate bodies keep
their existing dimensions as input counts change.
A thin component-colored vertical rail at the input anchors extends to the outer
input rows, and the existing stubs bridge it to the unchanged silhouette. The rail
is derived from aligned, automatically routed pins; authored irregular/explicit
interfaces retain their own presentation. It participates in picking, selection,
placement, and routing bounds, but creates no nets or electrical connections.
Presentation rebuilds leads, rail, and pin markers from the new layout. Existing
endpoint routing also handles changed
pin anchors at the same component origin, preserving retained logical drivers.
Removing a wired input is rejected, as are overlapping bodies or unavailable
routes. The popup reports the reason; rejected edits leave the scene/history
unchanged. Existing generic layout operations retain their explicit pin-removal
policy; only the simple input-count operation opts into this endpoint rerouting.

Headless `gate_settings` covers count limits, native paired types, labels,
Selection/Interaction behavior, wired growth/shrink and undo/redo, rejected wired
removal/overlap, and NOT/clock/latch restrictions. Framebuffer checks cover updated
pins and active/read-only controls at normal and 2x DPI. Previews are
`gate-settings.ppm`, `gate-settings-2x.ppm`, and their read-only equivalents.

Input-rail checks additionally verify native creation/editing without body stretch,
rail-aware bounds and movement previews, independent input drivers, and rail
removal after shrinking. Framebuffer captures compare each of AND/NAND/OR/NOR/
XOR/NXOR at two and eight inputs: body pixels are identical. The rail is rendered
in the existing lead pass with the component tint, before the body/pin markers,
and disappears from reused buffers after shrinking. Previews are `input-rail-*.ppm`.

### Shape-specific extensions and inversion

Paired native inversion changes the quad width by the bubble's 1.5x footprint
and moves the output anchor one grid cell, rerouting attached output wiring.
The solid body and bubble match spawning the target native gate in either
direction. Explicit saved geometry is restored with its inversion during creation,
so opening a file does not apply the width/anchor adjustment again.

Native input rows use one-cell spacing, keeping two or three inner inputs behind
the body. Their original input column stays fixed. White terminals only bridge
actual gaps. AND/NAND extensions sit just inside the flat back edge; OR/NOR
extensions start beyond the back corners and leave the concave middle open.
XOR/NXOR extend the rear arc vertically; outer inputs meet the arc, while inner
inputs connect to the solid body. The shader omits its short arc when the extended
arc is submitted. Adjacent batches distinguish that flag without reordering bodies.

The extended arc uses a continuous triangle ribbon with a one-framebuffer-pixel
alpha fringe on both the colored stroke and its outline. This reuses the existing
lead mesh/shader and avoids hard edges or overlapping feather strips at joints.
Pixel checks verify intermediate edge coverage and matching spawned/switched
gate silhouettes; headless checks keep input columns fixed and leads short.
