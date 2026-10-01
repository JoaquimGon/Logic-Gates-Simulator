# Logic Gates Simulator

A hardware-accelerated digital logic simulator written in C++20 and OpenGL 3.3 Core. Circuit connectivity is solved via a directed graph network evaluated through a Depth-First Search (DFS) topological sort, and rendering uses custom Signed Distance Field (SDF) shaders with analytical anti-aliasing.

---

## Features

- **Component Roster:** Interactive logic primitives:
  - Base gates: `AND`, `OR`, `XOR`, `NOT`
  - Inverted gates: `NAND`, `NOR`, `NXOR` (with dedicated inversion bubbles)
  - Interactive inputs: `InputPin` (clickable manual toggle switch)
- **Grid & Routing Engine:** Discrete integer coordinate snapping, collinear overlap merging, mid-wire branch-splitting, and automatic wire healing.
- **Topological Simulation:** Cycle-checked topological order calculation preventing evaluation crashes during combinational feedback loops.
- **SDF Graphics Pipeline:** Resolution-independent gate geometry rendered on dynamic quads with sub-pixel screen-space anti-aliasing (`fwidth`).
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
in Selection mode.


| Input | Action |
| :--- | :--- |
| **Left Click (Pin)** | Begin routing wire from an input/output pin |
| **Left Click (Wire)** | Branch or split an existing wire segment |
| **Left Click (Component Body)** | Selection: select/drag any component; Interaction: operate actionable components |
| **Left Click (InputPin Body)** | Selection: move without toggling; Interaction: toggle logic state |
| **Right Click (Drag)** | Pan view camera |
| **Right Click (Click)** | Cancel wire placement / Deselect |
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
    Views/             Component presentation and PinUI data
  Simulation/          Circuit propagation, nets, and endpoint identities
  Geometry/            Coordinates, wire normalization, hit/placement queries, and paths
  Graphics/            OpenGL drawing, meshes, shaders, and wire vertices
    Text/              Font atlas and text geometry
cmake/                 Dependencies, compiler options, and font discovery
assets/shaders/        GLSL presentations and live-reload sources
tests/                 Logic, editor action, and input regression runners
docs/                  Editor action contracts and integration examples
external/              Vendored GLAD and stb_truetype
```

Input and startup submit batches through EditorActions. Scene supplies committed
bounds and indexed pin anchors to Geometry services, then uses ConnectivityBuilder
to derive nets and Circuit edges from the normalized routes. Circuit evaluates the supported DAG, and
Scene synchronizes signal states for rendering. Invalid feedback pauses the
scene until the wiring is repaired.

CMake compiles shared `simulator_components`, `simulator_simulation`,
`simulator_geometry`, `simulator_scene`, and `simulator_input` libraries.
The application and tests link these libraries instead of recompiling separate
copies of their sources. Views form an interface target; the application also
links `simulator_graphics`.

Register new implementation files in their owning target in `CMakeLists.txt`.
Use forward-slash, source-root includes between modules, such as
`"Simulation/Circuit.h"`; same-directory includes can use `"Circuit.h"`.
Headers include their own requirements; implementation-only headers belong in
`.cpp` files. Native behavior and simulation must not include views, Graphics,
GLFW, or GLAD. Wire vertex generation belongs in Graphics, not the wire model.

Editor actions live under `Editor/Actions/`; concrete gesture handlers live under
`Editor/Gestures/` and require no GLFW. Input adapts events, applies the mode policy,
and dispatches shortcuts; native keyboard creation presets are in
`Editor/ComponentShortcuts.cpp` pending the component catalog. Geometry services
consume plain bounds/anchors without view or graphics dependencies. The builder
under `Editor/Connectivity/` consumes normalized routes and pin interfaces without
accessing Scene or healing geometry. Scene owns and publishes derived topology.
Reusable component definitions and catalog/default consolidation under
`Components/` remain pending. See [geometry and connectivity contracts](docs/SceneTopology.md).

## Dependencies & Vendoring

- C++20 and CMake 3.20+ (3.21+ for the version-3 presets).
- GLFW 3.4 and GLM 1.0.1: fetched by CMake; the first configure needs network
  access unless local sources are supplied through `FETCHCONTENT_SOURCE_DIR_GLFW`
  and `FETCHCONTENT_SOURCE_DIR_GLM`.
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
OpenGL, GLAD, stb_truetype, or font discovery; input tests still require GLFW's
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
component, and destination input index. Scene gate creation derives the logical
arity from its supplied input layout. Visual pins must have valid, unique indices
covering every logical pin; vector order does not determine pin identity.

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
mode isolation, immediate mode/focus cancellation, wire branching, and panning.
CTest runs all three runners (25 groups); use `ctest --test-dir out/build/x64-debug -R "drag_|spawn_|wire_segment_deletion" --output-on-failure`
to run only the input tests.

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

See [Editor action contracts and examples](docs/EditorActions.md) for API use,
pointer lifetimes, migration rules, and snapshot-restoration semantics.
`EditorActionsTests` covers these boundaries without a window or OpenGL context.

### Rejected Connections

Feedback connections, including self-loops, are unsupported by the current DAG
simulator. When a scene connection is rejected, simulation and automatic clock
advancement pause for the entire scene. Wiring remains editable, retained output
states are preserved, and pins/wires display no active signal. A red warning
appears even with the F3 debug HUD disabled; the console reports the rejected
net, component/pin endpoints, and reason. Repairing the wiring rebuilds the full
graph and resumes simulation automatically.

`Circuit::tryConnectComponents()` returns a typed `ConnectionResult`; the
existing `connectComponents()` boolean API remains available.
`Scene::getRejectedConnections()` exposes diagnostics from the latest rebuild.
