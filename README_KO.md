> 🌐 **언어 (Language):** [English](README.md) | **한국어**

# Metal Cube (3D Rotating Cube)

Apple Silicon Mac 환경에서 **순수 C++ (`metal-cpp`)**로 구현한 하드웨어 가속 3D 회전 큐브 애플리케이션입니다.

## 특징
- **모던 C++26 + MSL (Metal Shading Language)**:
  - Objective-C 브릿지 없이 Apple 공식 `metal-cpp` ([한국어 가이드](metal-cpp/README_KO.md) | [English Guide](metal-cpp/README.md)) 및 `metal-cpp-extensions` 직접 연동.
  - C++20/23/26 표준 기능 전면 적용: 다중 첨자 연산자 `operator[col, row]`, `NS::SharedPtr` 및 `std::unique_ptr` 기반 완전한 RAII(수동 `release`/`delete` 제거), `std::counting_semaphore`, `constexpr std::array`, `<numbers>`, `<print>`, C++26 `= delete("이유")` 및 플레이스홀더 `_`.
- **의미론적(Semantic) 0~9단계 아키텍처 분리**: [3D 공간 렌더링 의미론적 설계 문서](docs/3d-rendering-semantics.md)의 실행 환경 수명주기(0단계) 및 런타임 파이프라인(1~9단계)과 1:1로 대응하도록 모듈 분할.
  - `include/Math3D.hpp`: 기준 좌표계 전이(Change of Basis)를 위한 4x4 동차 좌표 행렬 연산
  - `include/World.hpp`: **[1~2단계]** 공간·개체의 존재론적 정의 및 프레임 독립적 시간 전이(`WorldState`)
  - `include/Observer.hpp`: **[3~4단계]** 관찰자(카메라) 시점 변환 및 원근 사영(`Observer`)
  - `include/Shaders.hpp`: **[2~4단계 & 7단계]** 정점 셰이더 내 단계별 좌표계 전이(`Local` → `World` → `View` → `Clip`) 및 프래그먼트 셰이더 광학적 상호작용 해석
  - `include/Renderer.hpp`: **[0단계 & 4~9단계]** 파이프라인 사전 컴파일·자원 계약, 후면 컬링, 이산화, 폐색 해결(Z-버퍼), 인지적 매핑(sRGB), CPU↔GPU In-flight 자원 격리(세마포어) 및 디스플레이 출력 동기화(`CubeRenderer`)
  - `metal_rotating_cube.cpp`: **[0단계 & 런타임 조율]** OS 창·표면·이벤트 루프 계약 및 프레임 루프 조율(`AppDelegate`, `ViewDelegate`)
- **VSCode 원클릭 디버깅**: `tasks.json` (Ninja 빌드) 및 `launch.json` (CodeLLDB) 연동으로 <kbd>F5</kbd> 단축키 즉시 구동.

## 빌드 및 실행

### 필수 요구사항
- macOS (Apple Silicon 권장)
- CMake 3.20+ & Ninja
- Clang / LLVM

### 터미널 빌드
```bash
# Debug 빌드
cmake -B build/debug -DCMAKE_BUILD_TYPE=Debug -G Ninja
ninja -C build/debug
./build/debug/metal_rotating_cube

# Release 빌드
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -G Ninja
ninja -C build/release
./build/release/metal_rotating_cube
```

### VSCode에서 실행
1. VSCode에서 `metal-cube` 폴더 열기: `code ~/repos/metal-cube`
2. <kbd>Cmd</kbd> + <kbd>Shift</kbd> + <kbd>B</kbd> 로 빌드 (Debug 또는 Release 선택)
3. <kbd>F5</kbd> 를 누르면 3D 큐브 창이 즉시 실행됩니다.

## 라이선스

코드는 [MIT License](LICENSE)를 따릅니다.
