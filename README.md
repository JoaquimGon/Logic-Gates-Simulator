# Logic Gates Simulator

A hardware-accelerated digital logic simulator written in C++20 and OpenGL 3.3 Core. Circuit signals settle through a bounded change-driven queue with support for feedback and chronological clock transitions, and rendering uses custom Signed Distance Field (SDF) shaders with analytical anti-aliasing.

---

## Features

- **Component Roster:** Interactive logic primitives:
  - Base gates: `AND`, `OR`, `XOR`, `NOT`
  - Inverted gates: `NAND`, `NOR`, `NXOR` (with dedicated inversion bubbles)
  - Interactive inputs: `InputPin` (clickable manual toggle switch)
  - Outputs: `OutputPin` (one-input bulb, dark for LOW and green for HIGH)
  - Input/output names: editable instance labels displayed beneath their symbols
- **Grid & Routing Engine:** Discrete integer coordinate snapping, collinear overlap merging, mid-wire branch-splitting, and automatic wire healing.
- **Feedback Simulation:** Changed signals propagate until stable; bounded settling pauses unstable circuits while keeping the editor responsive.
- **Clock Edges:** Every elapsed rising/falling transition settles the circuit before the next transition; simultaneous clocks advance together.
- **SDF Graphics Pipeline:** Resolution-independent gate geometry rendered on dynamic quads with sub-pixel screen-space anti-aliasing (`fwidth`).
- **Component Outlines:** Thin slate edges on basic components, inversion bubbles, pins, and latches, matching the unpowered output bulb fill.
- **Circuit Tabs:** Independent named scenes with retained cameras, a permanent Main tab, and simple rename/delete popups.
- **Live Shader Hot-Reloading:** Edit `.frag` or `.vert` files on disk; shaders recompile automatically at runtime.

---

## Controls & Keybindings *(Temporary)*

The application starts in **Selection mode**. Move/select/delete components,
create wires, and use spawn shortcuts in this mode; clicking an input body never
toggles its value. **Interaction mode** operates inputs and clock controls while
blocking structural edits. Simulation continues in both modes; pan and zoom are
available in both. The window title shows the mode and F2 shortcut.

F2 changes modes immediately and cancels unfinished dragging, wire drawing,
branching, and panning. Escape, right-click, focus loss, and scene switching also
discard unfinished gestures; a later mouse release cannot commit them.
Ctrl+Shift was avoided because Windows can reserve it for keyboard-layout changes.
Pin/wire routing, Delete/Backspace, and component spawn shortcuts below apply only
in Selection mode. UI adapters receive events first and can capture keyboard/pointer
input. Canvas bounds restrict gestures; UI focus cancels previews while retaining
selection for an inspector. See [input routing](docs/InputRouting.md) for integration
and [shared camera/viewport transforms](docs/CanvasCamera.md). Canvas layout now
controls rendering and picking together, including framebuffer scaling; screen
text remains independent.

The left component panel reserves 220 pixels of the window, leaving the rest for
the simulator. Its **Native** tab shows shader-preview cards with names below,
ordered Input, Output, Clock, NOT, AND, NAND, OR, NOR, XOR, NXOR, SR LATCH, D LATCH.
Latch previews show the latch name inside the body without pin labels. Cards use
two columns, or one in narrow windows. The **Custom** tab lists registered custom
definitions by name and input/output counts; it starts empty, with no sample circuits.
In Selection mode, press a card or custom row, drag onto the canvas,
and release to place it on the grid. An outline follows the proposed placement;
occupied space rejects the drop and shows a message. Escape, right-click, focus
loss, or resizing cancels the drag. Scroll inside the panel in shorter windows.
Debug metrics start hidden; F3 shows them.

The full-width top bar reserves 30 pixels. Click **File** to expand **Save**,
**Save As**, and **Open**, all operating on **Main only**. Save asks for a JSON
path the first time, then updates that file; Save As always asks for a path.
Open validates the file before replacing Main and switches to its tab. Other
views stay untouched. Windows uses the native file picker; cancelling it changes
nothing. Success/errors appear in the bar, with error details in the console.
Escape, outside clicks, focus loss, and resizing dismiss the dropdown.

The circuit strip reserves another 30 pixels below the top bar and above the canvas. Click the square **+**
after the last circuit tab to create and open an empty `unnamed`; click a
tab to switch.
Each circuit retains its components, wires, signal state, pan, and zoom. Only
the visible scene runs; inactive clocks freeze until that scene is revisited.
Tabs have a compact fixed width measured from `unnamed`, including padding and
space for the close button.
Long titles end in `...`; scroll over the strip when the tabs do not fit.
Hover a title for one second or right-click it to show its full name and a
prefilled name field. Click the field to replace the name, then Enter to save;
Backspace and Ctrl+A edit, while Escape, outside clicks, resize, and focus loss
cancel the draft. Names accept up to 64 printable ASCII characters. The popup
also shows its role; non-main views offer **Convert to subcircuit** or **Use as
workspace**, preserving the scene and its camera. Names and input/output
components never determine that role. Packaging into a reusable component is
not implemented yet. Click a circuit tab's **X**
to open a **Delete / Cancel** confirmation. Closing the active circuit switches
to the tab on its left; Main cannot be deleted. Escape or an outside click
cancels deletion. There is no saved-state check yet. Main is
saved as a single circuit design in JSON, including labels, settings, pin
layouts, manual input values, and wire bends. Clock phase, latch memory, and
derived signals reset on load. Subcircuit views remain in memory and are not
included. Subcircuit interfaces, packaging, and saving remain future work.
See [circuit files](docs/CircuitFiles.md) for the format and loading behavior.

A bottom panel sits beneath the simulator, beside the full-height component
panel. Its permanent **Tab 1** and **Tab 2** tabs retain placeholder names.
Tab 1 shows the active viewpoint's role and full name; Tab 2 is empty. The panel is up to
180 pixels tall and shrinks in shorter windows. It reserves canvas space and
consumes pointer/scroll events, so it cannot draw wires, place components, or
zoom the canvas. Functional tab names and contents will be added later.

Right-click an idle component or its pin to open a small information popup in
either mode. It shows the component name, body label when present, and live
input/output states (`0 (OFF)` or `1 (ON)`), with pin names or numbered fallbacks.
Blocked simulation states read `Unavailable`. Escape or a click outside closes
it; resize and focus loss also dismiss it. Scroll inside if the rows do not fit.
Pin editing and truth-table presentation remain future work.

Wire-point guides are larger and more opaque on hover. Pressing to draw or
branch keeps a highlighted dot at the starting point throughout the drag, with
another at the moving endpoint. Release clears creation highlights; after a
successful commit, normal hover highlighting resumes when leaving that grid cell.

To name an input or output in Selection mode, right-click it, click the **Name**
field, type, and press Enter to save. Backspace edits; Escape, outside clicks,
resize, or focus loss discard unfinished typing. Names accept up to 32 printable
ASCII characters; an empty name clears the label. Interaction mode shows the
name without editing it. Names preserve pin identities and wiring, and do not
need to be unique yet. Subcircuit interfaces and packaging remain future work.
Output bulbs have one incoming pin and no outgoing pins; unavailable signals
appear gray.

| Input | Action |
| :--- | :--- |
| **Click (Native / Custom Tab)** | Switch the component palette category in either mode |
| **Drag (Component Card / Custom Row)** | Selection: release on the canvas to create a component |
| **Left Click (Pin)** | Begin routing wire from an input/output pin |
| **Left Click (Wire)** | Branch or split an existing wire segment |
| **Left Click (Component Body)** | Selection: select/drag any component; Interaction: operate actionable components |
| **Left Click (InputPin Body)** | Selection: move without toggling; Interaction: toggle logic state |
| **Right Click (Component/Pin)** | Open live component information when no gesture is active |
| **Middle Mouse (Drag on Canvas)** | Pan view camera, including when starting over a component |
| **Right Click (Active Gesture)** | Cancel wire placement / component dragging |
| **Ctrl + Scroll** | Zoom in / Zoom out |
| **Delete / Backspace** | Delete selected component or wire segment |
| **Escape** | Abort current gesture |
| **F2** | Toggle Selection / Interaction mode |
| **F3** | Toggle debug metrics |
| **Space** | Interaction: pause/resume clocks |
| **Period** | Interaction: step clocks once |
| **1** | Spawn `InputPin` |
| **2** | Spawn `NOT` Gate |
| **3** | Spawn `AND` Gate |
| **4** | Spawn `NAND` Gate |
| **5** | Spawn `OR` Gate |
| **6** | Spawn `NOR` Gate |
| **7** | Spawn `XOR` Gate |
| **8** | Spawn `NXOR` Gate |
| **9** | Spawn clock |
| **U / I** | Spawn SR / D latch |

---

## Project Structure & Architecture

```text
src/
  App/                 Entry point, GLFW lifecycle, and application loop
  Editor/              Input gestures and Scene coordination
    Actions/           Typed edits, preview lifecycle, and reversible changes
    Gestures/          Component dragging, wire drawing, panning, and selection
    Connectivity/      Derived nets, pin/wire indexes, and edge rejection diagnostics
  Components/          Native behavior classes and logical pin types
    Definitions/       Built-in declarations, validated descriptors, and catalog
    Views/             Component presentation and PinUI data
  Simulation/          Circuit propagation, nets, and endpoint identities
  Geometry/            Coordinates, wire normalization, hit/placement queries, and paths
  Graphics/            OpenGL drawing, meshes, shaders, and wire vertices
    Presentation/      Typed canvas instances, batching, labels, and HUD layout
    Text/              CPU glyph geometry and context-owned atlas/text submission
  UI/                  Component palette, popups, tabs, and File menu
  Persistence/         Circuit JSON and file reading/writing without graphics dependencies
cmake/                 Dependencies, compiler options, and font discovery
assets/shaders/        GLSL presentations and live-reload sources
tests/                 Logic, editor, catalog, input, presentation, and pixel regressions
docs/                  Editor action contracts and integration examples
external/              Vendored GLAD and stb_truetype
```

Input and startup submit batches through EditorActions. Scene supplies committed
bounds and indexed pin anchors to Geometry services, then uses ConnectivityBuilder
to derive nets and Circuit edges from the normalized routes. Circuit settles changed
signals and advances clocks chronologically; Scene synchronizes states for rendering.
Connections permit feedback. Signals that exceed the settling limit pause simulation
until an input or edit requests another attempt.

CMake compiles shared `simulator_components`, `simulator_simulation`,
`simulator_geometry`, `simulator_component_factory`, `simulator_scene`, and `simulator_input` libraries.
The application and tests link these libraries instead of recompiling separate
copies of their sources. Views form an interface target; the application also
links `simulator_graphics`; CPU presentation functions form `simulator_presentation`.

Register new implementation files in their owning target in `CMakeLists.txt`.
Use forward-slash, source-root includes between modules, such as
`"Simulation/Circuit.h"`; same-directory includes can use `"Circuit.h"`.
Headers include their own requirements; implementation-only headers belong in
`.cpp` files. Native behavior and simulation must not include views, Graphics,
GLFW, or GLAD. Wire vertex generation belongs in Graphics, not the wire model.

Editor actions live under `Editor/Actions/`; concrete gesture handlers live under
`Editor/Gestures/` and require no GLFW. Input adapts events, applies the mode policy,
and dispatches shortcuts. `Editor/ComponentShortcuts.cpp` maps keys to definition IDs;
`Components/Definitions/NativeDefinitions.cpp` owns all built-in defaults, pins,
labels, and shader resources. `Components/ComponentFactory` creates matching logic
and views through the catalog. See [component definitions](docs/ComponentDefinitions.md)
for validated options, custom registration, and adding native declarations. Geometry services
consume plain bounds/anchors without view or graphics dependencies. The builder
under `Editor/Connectivity/` consumes normalized routes and pin interfaces without
accessing Scene or healing geometry. Scene owns and publishes derived topology.
Definition file import/export and extended educational inspection remain pending.
See [geometry and connectivity contracts](docs/SceneTopology.md).

`UI` lays out catalog buttons and handles one drag-to-create gesture. On release,
it submits `CreateComponent` through EditorActions. Drawing reuses screen
rectangles and text from Renderer; no widget framework or additional dependency
is introduced. Its input logic can be tested without an OpenGL context.
The same class handles the information popup, resolving its component ID on each
draw and reading synchronized pin data from the committed view. Its small
input/output name field submits the existing `ConfigureComponentProperties`
label operation through EditorActions.

Rendering adapts common view metadata into typed values once per frame. Bodies
carry individual sizes/tints; stable component-ID ordering and adjacent shader
batches preserve compositing. Renderer coordinates fixed canvas layers; separate
CPU functions lay out labels/overlays, and TextPainter owns GPU text submission.
See [rendering contracts and validation](docs/Rendering.md).

## Dependencies & Vendoring

- C++20 and CMake 3.20+ (3.21+ for the version-3 presets).
- GLFW 3.4 and GLM 1.0.1: fetched by CMake; the first configure needs network
  access unless local sources are supplied through `FETCHCONTENT_SOURCE_DIR_GLFW`
  and `FETCHCONTENT_SOURCE_DIR_GLM`.
- nlohmann/json 3.12.0: fetched from a release archive with a SHA-256 check;
  local sources can use `FETCHCONTENT_SOURCE_DIR_JSON`.
- GLAD: vendored in `external/glad/` for OpenGL 3.3 Core.
- stb_truetype 1.26: vendored in `external/stb_truetype.h`, including its license.
- OpenGL: supplied by the system driver for the application.
- A TrueType font: discovered at configure time or selected explicitly below.
  Fonts are not bundled.

## Building the Project

With a C++20 compiler and Ninja available, use the portable presets:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Run `out/build/debug/bin/Debug/LogicGateSimulator` (append `.exe` on Windows).
Use the `release` presets for a Release build. On Windows, run these commands
from an appropriate compiler environment, such as the Visual Studio Developer
Command Prompt.

For another generator or a custom build directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The Debug application is in `build/bin/Debug/`. Use `-DBUILD_TESTING=OFF` to omit
test executables. Use `-DBUILD_SIMULATOR_APP=OFF` for model/editor tests without
OpenGL, GLAD, or font discovery; presentation tests use vendored stb_truetype
metrics, and input tests still require GLFW's
null platform.

### Font Configuration

CMake searches common Windows, Linux, and macOS font locations and
`assets/fonts/`. Override the selection with an existing TrueType font:

```sh
cmake --preset debug -DLOGIC_SIMULATOR_FONT=/absolute/path/to/font.ttf
```

An unavailable font produces a configure error with the override instructions.
The selected path is cached per build directory; reconfigure to change it.
The application reads the selected font at runtime.

### Visual Studio Presets and Assets

The `x64-debug`, `x64-release`, `x86-debug`, and `x86-release` presets retain
MSVC configurations. Use a matching developer environment on the command line.
They no longer depend on a Visual Studio installation-specific CMake script.
Each configure preset has a matching build and test preset.

Shaders resolve against `PROJECT_ASSETS_DIR`, the source checkout's asset
directory, for live editing. Shader and font packaging for standalone releases
remains separate work.

## Roadmap

    [x] Primitive Gate Set (AND, OR, XOR, NOT, NAND, NOR, NXOR, InputPin)

    [x] SDF-based graphics pipeline with anti-aliasing

    [ ] Multi-driver short-circuit contention diagnostics

    [ ] Clock-driven components (Flip-Flops, Latches, Clocks)

    [ ] Sub-circuit modular packaging (Adders, Multiplexers, ALUs)

## Component Pins and Regression Tests

`Component` owns input and output state arrays; invalid pin access throws
`std::out_of_range`. Native counts are validated at creation. Gate input counts
can also change through a coherent editor configuration action.
`Gate` keeps one output and supports configurable input counts: NOT requires
one input, and AND/NAND/OR/NOR/XOR/NXOR accept two or more. XOR uses odd parity;
NXOR uses its inverse. Existing spawn shortcuts retain their default counts.

```cpp
Circuit circuit;
int latch = circuit.addLatch(LatchType::SR_LATCH);
int gate = circuit.addGate(GateType::AND, 4);
circuit.connectComponents(latch, 1, gate, 3); // ~Q -> fourth input
circuit.disconnectComponents(latch, 1, gate, 3);
```

Connections always specify source component, source output index, destination
component, and destination input index. Catalog creation resolves gate arity and
pin layout together; compatibility APIs validate supplied layouts through the same
factory. Visual pins carry stable definition IDs and valid directional indices;
vector order does not determine pin identity.

CTest builds a headless `LogicTests` executable by default. It covers gate truth
tables, pin bounds, both latch outputs, connection lifecycle, and scene pin mapping:

```powershell
cmake --build out/build/x64-debug
ctest --test-dir out/build/x64-debug --output-on-failure
```

For a generic build directory, substitute `build` and pass `-C Debug` to CTest
when using a multi-configuration generator. Use `-DBUILD_TESTING=OFF` to omit tests.

Input regression tests run in a separate `InputTests` executable using GLFW's
null platform, so they require no display, native window, or OpenGL context.
They cover Escape/right-click drag cancellation, movement and overlap rollback,
connectivity after every component spawn shortcut, middle-segment deletion,
mode isolation, immediate mode/focus cancellation, wire branching, panning, UI
capture, held-input cleanup, canvas bounds, panned/zoomed subcanvas editing, and
palette creation/cancellation/scrolling in both modes, and right-click information
with live indexed states, custom labels, dismissal, and preserved pan/cancel behavior.
`CameraTests` covers shared forward/inverse transforms, viewport clipping/rounding,
resizing, invalid/minimized layouts, and uniform/nonuniform DPI.
Catalog regressions validate built-in defaults/assets, custom box/native behavior,
registration rollback, applicable options, and catalog/identity restoration.
Presentation tests cover typed instances/batching, pin leads, label layout, and
world/screen glyph geometry. The application build also runs an invisible-window
framebuffer test; it skips when an OpenGL context is unavailable and writes PPM
previews under the build's `render-artifacts/` directory when exercised.
`SimulationTests` covers stable feedback, bounded oscillation/recovery, chronological
and simultaneous clocks, retained catch-up, rising/falling receivers, register
ordering, and a master/slave circuit made from existing D latches.
CTest runs 46 groups with the application, or 45 headlessly; use `ctest --test-dir out/build/x64-debug -R "drag_|spawn_|wire_segment_deletion|interaction_modes|mode_cancellation|wire_and_pan|ui_|canvas_|component_palette|component_information|component_naming" --output-on-failure`
to run only the input tests.
`component_outputs` checks passive output propagation, labels, and restoration;
`component_naming` checks text capture, submission, and cancellation.

## Shared Editor Actions

`EditorActions::apply()` stages typed create/move/configure/delete/wire operations
as one batch. Structural edits validate final placement and rebuild connectivity
once; failures leave the committed scene unchanged. Drag previews affect only
presentation, so cancellation needs no topology rebuild.

`ConfigureComponent` updates layouts and gate arity together. Surviving pin indices
retain identity; removing wired inputs requires an explicit attachment policy or
wire removal in the same batch. Pin layouts and committed wires are read-only to
callers. Complete before/after records preserve normalized wire IDs and runtime
state for future undo/redo; history controls and persistence remain pending.

`ConfigureComponentProperties` edits explicit optional fields: body label,
scalable input count (clamped 2–8), supported native inversion, clock frequency,
and pause. Omitted fields retain current values. Inversion updates logic and the
bubble together; failed batches roll back. The information popup shows live
pins/state and edits input/output names; truth tables and other limited edit
controls remain pending.

See [Editor action contracts and examples](docs/EditorActions.md) for API use,
pointer lifetimes, migration rules, and snapshot-restoration semantics.
`EditorActionsTests` covers these boundaries without a window or OpenGL context.

### Feedback and simulation status

Feedback connections, including self-loops, are accepted. Stable circuits settle
normally; a circuit exceeding the settling limit pauses automatic clocks, displays
unavailable signals, and shows a warning even with F3 hidden. Input changes or
edits request another attempt. Gate-built memory may need Set/Reset initialization.

Invalid endpoints or multiple drivers on one input still reject connectivity and
pause the scene. Wiring remains editable; the console reports rejected endpoints
and reasons. Repairing the wiring rebuilds connectivity and resumes simulation.
See [clock timing and feedback](docs/Simulation.md) for limits, catch-up behavior,
and the educational Boolean model.

`Circuit::tryConnectComponents()` returns a typed `ConnectionResult`; the
existing `connectComponents()` boolean API remains available.
`Scene::getRejectedConnections()` exposes diagnostics from the latest rebuild.
