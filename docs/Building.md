# Building, testing and packaging

## Requirements

- C++20 compiler, CMake 3.21+ for presets, and Ninja.
- OpenGL 3.3 and a TrueType font for the application.
- On Windows, Visual Studio C++ tools and a matching Developer PowerShell or Command Prompt.
  Use the x64 developer environment for the portable Windows preset.

CMake fetches GLFW 3.4, GLM 1.0.1 and nlohmann/json 3.12.0 on the first configuration.
GLAD and stb_truetype are vendored in `external/`. For offline builds, provide local
sources with `FETCHCONTENT_SOURCE_DIR_GLFW`, `FETCHCONTENT_SOURCE_DIR_GLM` and
`FETCHCONTENT_SOURCE_DIR_JSON`.

## Development builds

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Run `out/build/debug/bin/Debug/LogicGateSimulator` (`.exe` on Windows).
The `release` preset enables compiler optimizations. The `x64-debug`, `x64-release`,
`x86-debug` and `x86-release` presets select MSVC in the corresponding developer environment.
These development builds resolve shaders and fonts from their configured locations.
Use the portable preset below for distribution.

For another generator or a custom directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Use `-DBUILD_TESTING=OFF` to omit test executables, or
`-DBUILD_SIMULATOR_APP=OFF` for tests without OpenGL or font discovery.
Input tests still use GLFW's null platform. List the tests enabled in a build with
`ctest --preset debug -N`; framebuffer tests can skip when no graphics context is available.
A skipped framebuffer test does not validate rendering.

## Fonts

Development builds discover a font in standard system locations or `assets/fonts/`.
Override the cached choice with an existing file:

```sh
cmake --preset debug -DLOGIC_SIMULATOR_FONT=/absolute/path/to/font.ttf
```

Standalone Windows builds use an installed Windows font at runtime: Consolas,
Courier New or Arial. Fonts are not redistributed.

## Portable Windows release

From an **x64 Visual Studio Developer PowerShell** at the repository root:

```powershell
cmake --preset windows-portable
cmake --build --preset windows-portable
ctest --preset windows-portable
cmake --install out/build/windows-portable --component Runtime
```

This preset enables Release optimization, the static MSVC runtime, embedded shaders
and regression tests. Benchmarks are disabled. The Runtime install component contains
only `out/install/windows-portable/LogicGateSimulator.exe`. Tests, sources and
development libraries are excluded.

Copy that executable to an empty folder and run it. It needs no accompanying assets,
DLLs or fonts. Project and dependency notices are embedded and readable through
**File > About / Licenses**, or by launching the executable with `--licenses`.
The target PC needs Windows 10/11 x64 and an OpenGL 3.3 graphics driver.

Development builds retain shader loading and hot reload from disk. Standalone builds
use embedded shader sources, including nested GLSL includes, and do not poll external
shader files. Changing a shader or license notice triggers CMake regeneration so the
next build embeds the updated content.

For an automated startup check, run the copied executable with `--check-runtime`
from another working directory and check its exit code. It initializes a hidden window,
loads the embedded shaders and the system font, then exits. `--print-licenses` writes
the embedded notices to standard output for inspection. These checks complement the
regression suite; they do not replace interactive checks on another Windows machine.

Tag releases from the tested source commit on `main`, and attach the tested executable
to the matching GitHub release. Build outputs remain under ignored `out/`.
