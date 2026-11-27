> 🌐 **Language:** **English** | [한국어](README_KO.md)

# Metal Cube (Semantic 3D Cube — Metal · Vulkan · Direct3D 12)

A cross-platform 3D rotating cube project that shares a single **API-agnostic semantic core (Stages 1–4)** across three native backends — **Metal, Vulkan, and Direct3D 12** (Stage 0 & Stages 4–9) — for 1:1 architectural comparison.

## Architecture Overview
Modularized 1:1 according to the bootstrap lifecycle (Stage 0) and runtime pipeline (Stages 1–9) in the [3D Space Rendering Semantic Design Document](docs/3d-rendering-semantics.md).

- **Shared Core (`include/core/` — API-agnostic Stages 1–4 & Stage 0.5 Data ABI)**:
  - `include/core/Math3D.hpp`: 4x4 homogeneous coordinate matrix operations (supports Metal/DX12 and Vulkan NDC via `flipY`)
  - `include/core/World.hpp`: **[Stages 1–2]** Spatial ontology (`Vertex`, `kCubeVertices`, `kCubeIndices`) and frame-independent temporal transitions (`WorldState`)
  - `include/core/Observer.hpp`: **[Stages 3–4]** Observer view transformation and perspective projection (`Observer`)
  - `include/core/FrameUniforms.hpp`: **[Stage 0.5]** 208-byte CPU↔GPU uniform ABI shared by all backends
- **Three Pure Native Backends (`backends/` — zero bridges or external windowing libraries)**:
  - `backends/metal/` (**macOS Native**): `AppKit` + `MTK::View` + `metal-cpp` + MSL (`MetalShaders.hpp`, `MetalRenderer.hpp`, `main_metal.cpp`)
  - `backends/vulkan/` (**Windows Win32 · Linux X11 Native**): GLSL 450 → SPIR-V + explicit swapchain/resize renegotiation/staging/synchronization (`VulkanRenderer.hpp`, `main_vulkan_win32.cpp`, `main_vulkan_x11.cpp`)
  - `backends/dx12/` (**Windows Native**): `Win32` + `DXGI` Flip-Model (sRGB RTV) + `D3D12` + `D3DCompile` HLSL 5.0 (`Dx12Shaders.hpp`, `Dx12Renderer.hpp`, `main_dx12.cpp`)
- **Multi-OS Build CI**: [.github/workflows/ci.yml](.github/workflows/ci.yml) verifies native builds on macOS (Metal), Windows (DX12 + Vulkan), and Linux (Vulkan).

## Build and Run

CMake automatically detects the host OS and enables only the native backend targets for that platform.

### macOS (Apple Silicon — Metal Native)
```bash
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
ninja -C build/debug

./build/debug/metal_rotating_cube
```

### Windows (Direct3D 12 + Vulkan)
```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

.\build\Release\dx12_rotating_cube.exe
.\build\Release\vulkan_rotating_cube.exe
```

### Linux (Vulkan + X11)
```bash
sudo apt-get install -y ninja-build libvulkan-dev glslang-tools libx11-dev
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -G Ninja
ninja -C build/release
./build/release/vulkan_rotating_cube
```

## License

The code is licensed under the [MIT License](LICENSE).
