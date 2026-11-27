#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <type_traits>
#include "Math3D.hpp"

// [1단계: 공간과 개체의 존재론적 정의 (Spatial Ontology & State Representation)]
struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 3> color;
};

static_assert(std::is_standard_layout_v<Vertex>, "Vertex must maintain standard layout for GPU vertex buffer mapping.");

inline constexpr std::array<Vertex, 24> kCubeVertices = {{
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
}};

// 반시계 방향(CCW) 정점 감기 순서를 기준으로 표면의 앞면을 정의하는 위상 인덱스 배열
inline constexpr std::array<std::uint16_t, 36> kCubeIndices = {
    0,  1,  2,   2,  3,  0,
    4,  5,  6,   6,  7,  4,
    8,  9,  10,  10, 11, 8,
    12, 13, 14,  14, 15, 12,
    16, 17, 18,  18, 19, 16,
    20, 21, 22,  22, 23, 20
};

// [1단계 & 2단계: 공간의 상태와 시간 전이 (API 비종속)]
class WorldState {
public:
    WorldState() noexcept
        : _angle(0.0f),
          _lastTime(std::chrono::steady_clock::now()),
          _lightDirWorld{0.5f, 1.0f, 0.8f, 0.0f} {}

    void updateSimulation() noexcept {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - _lastTime).count();
        _lastTime = now;

        _angle += 0.9f * dt;
    }

    Matrix4x4 computeModelMatrix() const noexcept {
        Matrix4x4 rotX = Matrix4x4::rotationX(_angle * 0.8f);
        Matrix4x4 rotY = Matrix4x4::rotationY(_angle * 1.2f);
        return Matrix4x4::multiply(rotY, rotX);
    }

    constexpr std::array<float, 4> lightDirectionWorld() const noexcept {
        return _lightDirWorld;
    }

private:
    float _angle;
    std::chrono::steady_clock::time_point _lastTime;
    std::array<float, 4> _lightDirWorld;
};
