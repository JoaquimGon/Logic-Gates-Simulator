# Canvas Camera and Viewport

## Ownership and data flow

`Geometry/CanvasCamera` owns the world center, zoom, and optional logical viewport.
It depends on GLM, not GLFW or OpenGL. `Input` owns the application's camera and
adapts GLFW window/framebuffer sizes into `CanvasSurface`. Engine passes
`Input::getCameraFrame(window)` directly to `Renderer::beginFrame()`.

`CanvasCameraFrame` contains the resolved logical rectangle, bottom-left framebuffer
rectangle, world-to-NDC matrix, inverse matrix, and framebuffer pixels per world
unit. Picking, creation, wire routing, and panning use its conversions. Component,
wire/lead, pin/junction/highlight shaders and world text use its forward matrix;
the procedural grid uses its inverse and `GridMetrics::Spacing`.

## Using a smaller canvas

```cpp
input.setCanvasViewport(CanvasViewport{240, 48, 960, 720});
input.setZoom(1.5f);
input.setPanOffset({0.25f, 0.1f});
renderer.beginFrame(input.getCameraFrame(window));
renderer.drawCanvas(frame);
// Full-window screen-space UI/text follows the canvas pass.
```

`setCanvasViewport()` replaces RM-U1's input-only `setCanvasInputBounds()` name;
the rectangle now controls drawing and picking together. `getCanvasViewport()`
returns the requested layout; `getCameraFrame().viewport` is the effective layout.
`std::nullopt` follows the full current window. UI adapters must update a custom
layout when panel/window sizes change; a supplied rectangle is not automatically
anchored to a window edge.

Coordinates use GLFW logical window units, top-left origin. The request is clipped
to the window, converted to framebuffer pixels, and its edges rounded to the
nearest pixel. Input uses those same effective edges (left/top inclusive,
right/bottom exclusive). Fractional layouts can shift by at most half a framebuffer
pixel. Camera aspect and point diameters use the physical viewport, keeping grid
cells square even with different horizontal/vertical scale factors.

Zoom 1 shows a vertical world span of 2; zoom 2 shows a span of 1. Pan is the world
position at the canvas center. Panning compares the grabbed/current cursor's inverse
projections so that world point stays under the cursor; scrolling retains the
existing center-based Ctrl+wheel zoom and 0.2-5 clamp.

## Rendering and interruption rules

Renderer clears the full target at `beginFrame()`. Every world pass applies the
canvas viewport and scissor, including point sprites and world text. `drawCanvas()`
restores the full framebuffer viewport and disables scissoring for subsequent UI.
Screen text uses logical full-window coordinates and remains independent of pan,
zoom, and canvas layout; direct world draw methods retain canvas state until the
next screen/canvas completion pass.

Changing layout or programmatic camera values cancels unfinished gestures while
retaining selection. Input also detects window/framebuffer size changes before
canvas events or per-frame processing, cancelling stale previews and releases.
Negative/non-finite layout, non-finite center, and non-positive/non-finite zoom are
rejected before mutation. Empty/minimized/offscreen or unrepresentable transforms
produce an inactive frame: world drawing and pointer input are skipped.

## Verification

`camera_transforms` checks known extents, matrix/conversion agreement, panned/zoomed
landscape/portrait canvases, fractional layouts, and uniform/nonuniform DPI scales.
`camera_viewport_layout` checks clipping, rounding, invalid values, and inactive
surfaces. `canvas_camera_interaction` exercises actual GLFW picking, dragging,
spawning, wiring, panning, and resize cancellation in a subcanvas.

`render_pixels` additionally verifies grid alignment, transformed components,
leads/wires/points, clipping of highlights and world text, and screen overlay
independence on a real OpenGL framebuffer. It writes `canvas-viewport.ppm` and
`canvas-with-overlay.ppm` under the build's `render-artifacts/` directory.

```sh
ctest --preset x64-debug -R "camera_|canvas_|render_pixels"
```

These tests include simulated high-DPI targets; moving a visible application across
monitors and toolkit-specific panel layout still need integration verification.
