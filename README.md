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
- **Intentional Omissions**: Because this sample renders a single cube, **Stage 4 Frustum Culling** and **Stage 5 MSAA** are intentionally omitted across all three backends.

## Build and Run

CMake automatically detects the host OS and enables only the native backend targets for that platform.

### macOS (Apple Silicon — Metal Native)
```bash
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
ninja -C build/debug

./build/debug/metal_rotating_cube
```

### Windows (Direct3D 12 + Vulkan)

#### Prerequisites (One-Time Setup)
1. **C++ Toolchain (Required for both DX12 & Vulkan)**
   - **Visual Studio 2022** (or Build Tools 2022) with the **"Desktop development with C++"** workload (includes MSVC + Windows 10/11 SDK)
   - **CMake 3.20+** (`winget install Kitware.CMake` or Visual Studio bundled CMake)
   - *Direct3D 12 (`d3d12`, `dxgi`, `d3dcompiler`) is built into the Windows SDK — no separate graphics SDK is needed.*
2. **LunarG Vulkan SDK (Required for Vulkan)**
   - Install from [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows) or run `winget install KhronosGroup.VulkanSDK`
   - Open a **new terminal** and verify `VULKAN_SDK` is set (`echo $env:VULKAN_SDK`)
   - Up-to-date GPU drivers (NVIDIA / AMD / Intel) with Vulkan runtime support.

#### Build & Run (Both DX12 and Vulkan)
```powershell
git pull
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run Direct3D 12
.\build\Release\dx12_rotating_cube.exe

# Run Vulkan (shaders/*.spv are copied next to the executable automatically)
.\build\Release\vulkan_rotating_cube.exe
```

#### Build & Run Direct3D 12 Only (Without Vulkan SDK)
```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_VULKAN=OFF
cmake --build build --config Release --target dx12_rotating_cube
.\build\Release\dx12_rotating_cube.exe
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
