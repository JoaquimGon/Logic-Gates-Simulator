# Logic Gates Simulator

A digital logic circuit editor and simulator built with C++20 and OpenGL 3.3.

## Download and run

Download **LogicGateSimulator.exe** from [GitHub Releases](https://github.com/JoaquimGon/Logic-Gates-Simulator/releases/latest)
and double-click it. No installation, extraction or compilation is needed.

Requires Windows 10/11 (64-bit) and a graphics driver supporting OpenGL 3.3.

## Features

- Logic gates, inputs, outputs, clocks, latches and flip-flops.
- Circuit editing with automatic wire routing, copy/paste and undo/redo.
- Multiple circuit tabs, JSON saving/loading and reusable nested subcircuits.
- Clock and signal waveforms, component settings, and truth/next-state tables.
- Four editable examples in **File > Examples**.

## Basic controls

| Action | Control |
| --- | --- |
| Switch Selection / Interaction mode | F2 |
| Select or move components; draw wires | Left mouse button in Selection mode |
| Toggle an input | Click it in Interaction mode |
| Component information and settings | Right-click a component |
| Pan / zoom | Middle mouse drag / scroll |
| Undo / redo | Ctrl+Z / Ctrl+Y |
| Copy / paste | Ctrl+C / Ctrl+V |
| Fit the circuit in view | Home |

Menus show additional commands and shortcuts. See [input and editing controls](docs/InputRouting.md)
for selection, wire gestures and other interactions.

## Build from source

Requires a C++20 compiler, CMake 3.21+, Ninja, OpenGL and a TrueType font.
CMake fetches GLFW, GLM and nlohmann/json during the first configuration.
On Windows, use a Visual Studio Developer PowerShell with the C++ tools installed.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

For a shareable Windows build, use the `windows-portable` preset.
See [build, test and packaging instructions](docs/Building.md) for the complete steps.

## Documentation

- [Examples](docs/Examples.md) and [truth tables](docs/TruthTables.md)
- [Circuit files](docs/CircuitFiles.md) and [subcircuits](docs/Subcircuits.md)
- [Simulation and clocks](docs/Simulation.md), [flip-flops](docs/FlipFlops.md) and [signal traces](docs/SignalTraces.md)
- [Editor actions and history](docs/EditorActions.md) and [input routing](docs/InputRouting.md)
- [Component definitions](docs/ComponentDefinitions.md) and [scene topology](docs/SceneTopology.md)
- [Rendering](docs/Rendering.md) and [camera](docs/CanvasCamera.md)
- [Performance benchmarks](benchmarks/README.md)

## License

[MIT](LICENSE). Project and dependency notices are available in **File > About / Licenses**
and [THIRD-PARTY-NOTICES.txt](packaging/THIRD-PARTY-NOTICES.txt).
