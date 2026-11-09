> 🌐 **Language:** **English** | [한국어](README_KO.md)

# Metal Cube (3D Rotating Cube)

A hardware-accelerated 3D rotating cube application written in **pure C++ (`metal-cpp`)** for Apple Silicon Macs.

## Features
- **Modern C++26 + MSL (Metal Shading Language)**:
  - Direct integration with Apple's official `metal-cpp` ([English Guide](metal-cpp/README.md) | [한국어 가이드](metal-cpp/README_KO.md)) and `metal-cpp-extensions` without Objective-C bridges.
  - Idiomatic C++20/23/26 features: multidimensional subscript `operator[col, row]`, full RAII via `NS::SharedPtr` & `std::unique_ptr` (zero manual `release`/`delete`), `std::counting_semaphore`, `constexpr std::array`, `<numbers>`, `<print>`, C++26 `= delete("reason")`, and placeholder `_`.
- **9-Stage Semantic Architecture Modularization**: Modularized 1:1 according to the [3D Space Rendering Semantic Design Document](docs/3d-rendering-semantics.md).
  - `include/Math3D.hpp`: 4x4 homogeneous coordinate matrix operations for reference frame transformations (Change of Basis)
  - `include/World.hpp`: **[Stages 1–2]** Spatial ontology definitions and frame-independent temporal state transitions (`WorldState`)
  - `include/Observer.hpp`: **[Stages 3–4]** Observer (camera) view transformation and 3D $\to$ 2D perspective projection (`Observer`)
  - `include/Shaders.hpp`: **[Stages 4 & 7]** Step-by-step coordinate space transitions (`Local` $\to$ `World` $\to$ `View` $\to$ `Clip`) in the vertex shader and optical interaction evaluation in the fragment shader
  - `include/Renderer.hpp`: **[Stages 4–9]** Back-face culling, spatial discretization, occlusion resolution (Z-buffer), perceptual mapping (sRGB), triple-buffered (`Triple Buffering` + semaphore) temporal isolation, and physical signal presentation (`CubeRenderer`)
  - `metal_rotating_cube.cpp`: Runtime frame loop orchestration (`ViewDelegate`) and native window initialization
- **One-Click Debugging in VSCode**: Instant execution via <kbd>F5</kbd> using `tasks.json` (Ninja build) and `launch.json` (CodeLLDB).

## Build and Run

### Requirements
- macOS (Apple Silicon recommended)
- CMake 3.20+ & Ninja
- Clang / LLVM

### Terminal Build
```bash
# Debug build
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
ninja -C build/debug
./build/debug/metal_rotating_cube

# Release build
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -G Ninja
ninja -C build/release
./build/release/metal_rotating_cube
```

### Run in VSCode
1. Open the `metal-cube` folder in VSCode: `code ~/repos/metal-cube`
2. Press <kbd>Cmd</kbd> + <kbd>Shift</kbd> + <kbd>B</kbd> to build (select Debug or Release)
3. Press <kbd>F5</kbd> to launch the 3D cube window immediately.
