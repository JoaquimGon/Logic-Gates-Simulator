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

| Input | Action |
| :--- | :--- |
| **Left Click (Pin)** | Begin routing wire from an input/output pin |
| **Left Click (Wire)** | Branch or split an existing wire segment |
| **Left Click (Gate Body)** | Select component / Drag to move |
| **Left Click (InputPin Body)** | Toggle logic state (`HIGH` / `LOW`) |
| **Right Click (Drag)** | Pan view camera |
| **Right Click (Click)** | Cancel wire placement / Deselect |
| **Ctrl + Scroll** | Zoom in / Zoom out |
| **Delete / Backspace** | Delete selected component or wire segment |
| **Escape** | Abort current interaction |
| **1** | Spawn `InputPin` |
| **2** | Spawn `NOT` Gate |
| **3** | Spawn `AND` Gate |
| **4** | Spawn `NAND` Gate |
| **5** | Spawn `OR` Gate |
| **6** | Spawn `NOR` Gate |
| **7** | Spawn `XOR` Gate |
| **8** | Spawn `NXOR` Gate |

---

## Project Structure & Architecture

The simulator bridges geometric layout with directed graph simulation:

```text
  User Input / View (Input.cpp, ComponentView, Wire)
         │
         ▼
  Geometry Layer (Scene.cpp)
  ├── Grid Snapping & Hit Testing
  ├── Wire Path Splitting & Healing
  └── Pin-to-Pin Topology Mappings
         │
         ▼
  Logical Circuit (Circuit.cpp)
  ├── Directed Acyclic Graph (DAG) validation
  ├── Cycle Detection (DFS recursion stack)
  ├── Topological Sort (Evaluation Order)
  └── Forward State Propagation
         │
         ▼
  Renderer (Renderer.cpp & SDF Shaders)
  └── Instanced Quad & Point Draws + Live GLSL Hot-Reload
  ```

## Dependencies & Vendoring
C++ Standard: C++20

  CMake: Version 3.20 or newer

    GLFW (3.4): Fetched automatically via FetchContent (requires internet on first configure)

    GLM (1.0.1): Fetched automatically via FetchContent

    OpenGL: System driver

    GLAD (OpenGL 3.3 Core): Vendored directly in the repository under external/glad/. No external generator or local package manager required on clone.


## Building the Project
Command Line (All Platforms)

Ensure a C++20-compliant compiler (MSVC 19.29+, GCC 11+, or Clang 13+) and CMake 3.20+ are available on your path:

```
# 1. Clone repository
git clone [https://github.com/](https://github.com/)<your-username>/Logic-Gates-Simulator.git
cd Logic-Gates-Simulator

# 2. Configure build system (requires network for GLFW/GLM FetchContent)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 3. Compile executable
cmake --build build --config Debug

# 4. Run binary
# On Windows:
./build/bin/Debug/LogicGateSimulator.exe
# On Linux/macOS:
./build/bin/LogicGateSimulator
```

## Visual Studio / VS Code CMake Presets

If using Visual Studio or VS Code with CMakePresets.json:

Launch from the Developer Command Prompt for VS if using command-line MSVC toolchains.

Select the x64-Debug or x64-Release preset.

Shaders are resolved directly against the absolute repository path defined by CMake (PROJECT_ASSETS_DIR), allowing live edits to save and reload without copying artifacts.

## Roadmap

    [x] Primitive Gate Set (AND, OR, XOR, NOT, NAND, NOR, NXOR, InputPin)

    [x] SDF-based graphics pipeline with anti-aliasing

    [ ] Multi-driver short-circuit contention diagnostics

    [ ] Clock-driven components (Flip-Flops, Latches, Clocks)

    [ ] Sub-circuit modular packaging (Adders, Multiplexers, ALUs)
