#pragma once

#include <chrono>
#include <cstdint>
#include "Math3D.hpp"

// [1단계: 공간과 개체의 존재론적 정의 (Spatial Ontology & State Representation)]
// 가상 세계 내에 존재하는 개체의 최소 구성 단위(정점)를 정의합니다.
// 기하학적 위치(position), 표면이 바라보는 방향(normal), 표면의 고유 반사율(color/albedo)을
// 외부 환경과 무관한 개체 고유 공간(Local Space) 기준으로 기술합니다.
struct Vertex {
    float position[3];
    float normal[3];
    float color[3];
};

// 개체 고유 공간(Local Space)의 원점(0, 0, 0)을 기준으로 정의된 정육면체 기하 및 물성 데이터
// 각 면이 서로 다른 법선(Normal)과 고유 색상(Albedo)을 가지므로 면당 4개씩 총 24개 정점으로 구성됩니다.
inline const Vertex kCubeVertices[24] = {
    // 앞면 (Front, +Z) - 빨강 계열
    { {-0.7f, -0.7f,  0.7f}, { 0.0f,  0.0f,  1.0f}, {0.95f, 0.25f, 0.25f} },
    { { 0.7f, -0.7f,  0.7f}, { 0.0f,  0.0f,  1.0f}, {0.95f, 0.25f, 0.25f} },
    { { 0.7f,  0.7f,  0.7f}, { 0.0f,  0.0f,  1.0f}, {0.95f, 0.25f, 0.25f} },
    { {-0.7f,  0.7f,  0.7f}, { 0.0f,  0.0f,  1.0f}, {0.95f, 0.25f, 0.25f} },

    // 뒷면 (Back, -Z) - 청록 계열
    { { 0.7f, -0.7f, -0.7f}, { 0.0f,  0.0f, -1.0f}, {0.15f, 0.85f, 0.85f} },
    { {-0.7f, -0.7f, -0.7f}, { 0.0f,  0.0f, -1.0f}, {0.15f, 0.85f, 0.85f} },
    { {-0.7f,  0.7f, -0.7f}, { 0.0f,  0.0f, -1.0f}, {0.15f, 0.85f, 0.85f} },
    { { 0.7f,  0.7f, -0.7f}, { 0.0f,  0.0f, -1.0f}, {0.15f, 0.85f, 0.85f} },

    // 윗면 (Top, +Y) - 초록 계열
    { {-0.7f,  0.7f,  0.7f}, { 0.0f,  1.0f,  0.0f}, {0.25f, 0.90f, 0.35f} },
    { { 0.7f,  0.7f,  0.7f}, { 0.0f,  1.0f,  0.0f}, {0.25f, 0.90f, 0.35f} },
    { { 0.7f,  0.7f, -0.7f}, { 0.0f,  1.0f,  0.0f}, {0.25f, 0.90f, 0.35f} },
    { {-0.7f,  0.7f, -0.7f}, { 0.0f,  1.0f,  0.0f}, {0.25f, 0.90f, 0.35f} },

    // 아랫면 (Bottom, -Y) - 노랑 계열
    { {-0.7f, -0.7f, -0.7f}, { 0.0f, -1.0f,  0.0f}, {0.95f, 0.85f, 0.20f} },
    { { 0.7f, -0.7f, -0.7f}, { 0.0f, -1.0f,  0.0f}, {0.95f, 0.85f, 0.20f} },
    { { 0.7f, -0.7f,  0.7f}, { 0.0f, -1.0f,  0.0f}, {0.95f, 0.85f, 0.20f} },
    { {-0.7f, -0.7f,  0.7f}, { 0.0f, -1.0f,  0.0f}, {0.95f, 0.85f, 0.20f} },

    // 오른쪽면 (Right, +X) - 파랑 계열
    { { 0.7f, -0.7f,  0.7f}, { 1.0f,  0.0f,  0.0f}, {0.25f, 0.45f, 0.95f} },
    { { 0.7f, -0.7f, -0.7f}, { 1.0f,  0.0f,  0.0f}, {0.25f, 0.45f, 0.95f} },
    { { 0.7f,  0.7f, -0.7f}, { 1.0f,  0.0f,  0.0f}, {0.25f, 0.45f, 0.95f} },
    { { 0.7f,  0.7f,  0.7f}, { 1.0f,  0.0f,  0.0f}, {0.25f, 0.45f, 0.95f} },

    // 왼쪽면 (Left, -X) - 보라 계열
    { {-0.7f, -0.7f, -0.7f}, {-1.0f,  0.0f,  0.0f}, {0.85f, 0.25f, 0.85f} },
    { {-0.7f, -0.7f,  0.7f}, {-1.0f,  0.0f,  0.0f}, {0.85f, 0.25f, 0.85f} },
    { {-0.7f,  0.7f,  0.7f}, {-1.0f,  0.0f,  0.0f}, {0.85f, 0.25f, 0.85f} },
    { {-0.7f,  0.7f, -0.7f}, {-1.0f,  0.0f,  0.0f}, {0.85f, 0.25f, 0.85f} }
};

// 정점들을 연결하여 12개의 삼각형 표면을 형성하는 위상(Topology) 인덱스 배열
// 반시계 방향(Counter-Clockwise) 정점 감기 순서를 기준으로 표면의 앞면을 정의합니다.
inline const uint16_t kCubeIndices[36] = {
    0,  1,  2,   2,  3,  0,  // 앞면
    4,  5,  6,   6,  7,  4,  // 뒷면
    8,  9,  10,  10, 11, 8,  // 윗면
    12, 13, 14,  14, 15, 12, // 아랫면
    16, 17, 18,  18, 19, 16, // 오른쪽면
    20, 21, 22,  22, 23, 20  // 왼쪽면
};

// [1단계 & 2단계: 공간의 상태와 시간 전이 (Spatial State & Temporal Transition)]
// 렌더링 API와 완전히 분리된 가상 세계의 순수 상태 및 시뮬레이션 모듈입니다.
// 화면 출력 여부와 무관하게 시간 흐름(dt)에 따라 세계 내부 상태를 전이시키고,
// 개체 고유 공간(Local Space)을 세계 공간(World Space)으로 배치하는 변환 행렬을 산출합니다.
class WorldState {
public:
    WorldState()
        : _angle(0.0f),
          _lastTime(std::chrono::high_resolution_clock::now()),
          _lightDirWorld{0.5f, 1.0f, 0.8f, 0.0f} {}

    // [2단계: 시간의 진행과 상태 전이]
    // 이전 시점으로부터 경과한 시간(dt)을 측정하여 프레임 주사율에 독립적으로 회전 상태를 갱신합니다.
    void updateSimulation() {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - _lastTime).count();
        _lastTime = now;

        _angle += 0.9f * dt;
    }

    // [2단계: 개체 고유 공간(Local Space) -> 세계 공간(World Space) 배치]
    // 현재 시점의 회전 상태를 바탕으로 개체를 세계 공간에 배치하는 Model 행렬을 반환합니다.
    Matrix4x4 computeModelMatrix() const {
        Matrix4x4 rotX = Matrix4x4::rotationX(_angle * 0.8f);
        Matrix4x4 rotY = Matrix4x4::rotationY(_angle * 1.2f);
        return Matrix4x4::multiply(rotY, rotX);
    }

    const float* lightDirectionWorld() const {
        return _lightDirWorld;
    }

private:
    float _angle;
    std::chrono::high_resolution_clock::time_point _lastTime;
    float _lightDirWorld[4]; // 세계 공간 내 광원이 비추는 방향 벡터 (16바이트 정렬)
};
