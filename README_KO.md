> 🌐 **언어 (Language):** [English](README.md) | **한국어**

# Metal Cube (Semantic 3D Cube — Metal · Vulkan · Direct3D 12)

하나의 **API 비종속 의미론적 코어(1–4단계)** 위에 **Metal · Vulkan · Direct3D 12** 세 네이티브 백엔드(0단계 & 4–9단계)를 1:1로 대조 구현한 크로스플랫폼 3D 회전 큐브 프로젝트입니다.

## 아키텍처 개요
[3D 공간 렌더링 의미론적 설계 문서](docs/3d-rendering-semantics.md)의 실행 환경 수명주기(0단계) 및 런타임 파이프라인(1–9단계)과 1:1로 대응하도록 모듈을 분리했습니다.

- **공통 코어 (`include/core/` — API 비종속 1–4단계 & 0.5단계 데이터 ABI)**:
  - `include/core/Math3D.hpp`: 기준 좌표계 전이를 위한 4x4 동차 좌표 행렬 연산 (`flipY` 옵션으로 Metal/DX12 및 Vulkan NDC 규약 동시 지원)
  - `include/core/World.hpp`: **[1–2단계]** 공간·개체의 존재론적 정의(`Vertex`, `kCubeVertices`, `kCubeIndices`) 및 프레임 독립적 시간 전이(`WorldState`)
  - `include/core/Observer.hpp`: **[3–4단계]** 관찰자 시점 변환 및 원근 사영(`Observer`)
  - `include/core/FrameUniforms.hpp`: **[0.5단계]** 전 백엔드가 공유하는 208바이트 CPU↔GPU 상수 버퍼 ABI
- **백엔드 3갈래 (`backends/` — 브릿지·외부 창 라이브러리 없이 순수 OS 네이티브)**:
  - `backends/metal/` (**macOS 네이티브**): `AppKit` + `MTK::View` + `metal-cpp` + MSL (`MetalShaders.hpp`, `MetalRenderer.hpp`, `main_metal.cpp`)
  - `backends/vulkan/` (**Windows Win32 · Linux X11 네이티브**): GLSL 450 → SPIR-V + 명시적 스왑체인/리사이즈 재협상/스테이징/동기화 (`VulkanRenderer.hpp`, `main_vulkan_win32.cpp`, `main_vulkan_x11.cpp`)
  - `backends/dx12/` (**Windows 네이티브**): `Win32` + `DXGI` Flip-Model(sRGB RTV) + `D3D12` + `D3DCompile` HLSL 5.0 (`Dx12Shaders.hpp`, `Dx12Renderer.hpp`, `main_dx12.cpp`)
- **멀티OS 자동 빌드 CI**: [.github/workflows/ci.yml](.github/workflows/ci.yml)에서 macOS(Metal), Windows(DX12 + Vulkan), Linux(Vulkan) 네이티브 빌드를 자동 검증합니다.
- **의도적 생략 범위**: 단일 정육면체 예제이므로 4단계의 **시야 절두체 선별(Frustum Culling)**과 5단계의 **다중 표본화(MSAA)**는 세 백엔드 모두 의도적으로 생략했습니다.

## 빌드 및 실행

CMake가 운영체제를 자동 감지하여 해당 OS의 순수 네이티브 백엔드만 활성화합니다.

### macOS (Apple Silicon — Metal 네이티브)
```bash
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
ninja -C build/debug

./build/debug/metal_rotating_cube
```

### Windows (Direct3D 12 + Vulkan)

#### 선제 작업 (최초 1회)
1. **C++ 빌드 툴체인 (필수 — DX12 · Vulkan 공통)**
   - **Visual Studio 2022** (또는 Build Tools 2022)에서 **"C++를 사용한 데스크톱 개발"** 워크로드 설치 (MSVC + Windows 10/11 SDK 포함)
   - **CMake 3.20+** (`winget install Kitware.CMake` 또는 Visual Studio 내장 CMake)
   - *Direct3D 12(`d3d12`, `dxgi`, `d3dcompiler`)는 Windows SDK에 기본 포함되어 별도 그래픽스 SDK가 필요 없습니다.*
2. **LunarG Vulkan SDK (Vulkan 빌드 시 필수)**
   - [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home#windows) 설치 또는 `winget install KhronosGroup.VulkanSDK`
   - 설치 후 **새 터미널**을 열어 `VULKAN_SDK` 환경 변수가 반영되었는지 확인 (`echo $env:VULKAN_SDK`)
   - 최신 GPU 드라이버(NVIDIA / AMD / Intel)에 Vulkan 런타임이 포함되어 있어야 실행됩니다.

#### 빌드 및 실행 (DX12 + Vulkan 모두)
```powershell
git pull
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Direct3D 12 실행
.\build\Release\dx12_rotating_cube.exe

# Vulkan 실행 (빌드 시 shaders/*.spv가 실행 파일 옆에 자동 복사됨)
.\build\Release\vulkan_rotating_cube.exe
```

#### Vulkan SDK 없이 Direct3D 12만 먼저 빌드·실행할 때
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

## 라이선스

코드는 [MIT License](LICENSE)를 따릅니다.
