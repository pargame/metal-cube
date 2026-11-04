# Metal Cube (3D Rotating Cube)

Apple Silicon Mac 환경에서 **순수 C++ (`metal-cpp`)**로 구현한 하드웨어 가속 3D 회전 큐브 애플리케이션입니다.

## 특징
- **100% C++17/26 + MSL (Metal Shading Language)**: Objective-C 브릿지 없이 Apple 공식 `metal-cpp` 및 `metal-cpp-extensions` 직접 연동.
- **3D 수학 파이프라인**: Column-Major 4x4 행렬 (회전, 이동, Metal [0, 1] NDC 원근 투영).
- **하드웨어 가속 렌더링**:
  - Z-버퍼(Depth32Float) 깊이 테스트 및 후면 제거(Backface Culling).
  - 람베르트(Lambert) 디퓨즈 + 앰비언트 3D 조명 셰이딩.
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
