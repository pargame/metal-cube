#pragma once

#include <numbers>
#include "Math3D.hpp"

// [3단계: 관찰자의 정립과 시점 변환] & [4단계: 차원 축소(원근 사영) 규격 정의]
// 객관적인 가상 세계 공간(World Space)을 특정 위치와 시야각으로 관측하는 주체(카메라/센서)를 정의합니다.
class Observer {
public:
    constexpr Observer() noexcept
        : _distanceZ(-3.2f),
          _fovYRadians(60.0f * (std::numbers::pi_v<float> / 180.0f)),
          _nearZ(0.1f),
          _farZ(100.0f) {}

    // [3단계: 세계 공간(World Space) -> 관찰자 공간(View Space) 시점 변환]
    // 관찰자의 위치를 원점(0, 0, 0)으로 삼아 세계 내 모든 개체의 좌표를 상대적으로 재정렬합니다.
    constexpr Matrix4x4 computeViewMatrix() const noexcept {
        return Matrix4x4::translation(0.0f, 0.0f, _distanceZ);
    }

    // [4단계: 관찰자 공간(View Space, 3D) -> 정규화 투영 공간(Clip/NDC Space, 2D + Depth) 차원 축소]
    // 관찰자의 시야 절두체(Frustum) 내부 3차원 공간을 2차원 평면과 깊이 범위([0, 1])로 압축합니다.
    Matrix4x4 computeProjectionMatrix(float aspectRatio) const noexcept {
        float safeAspect = (aspectRatio > 0.0f) ? aspectRatio : 1.0f;
        return Matrix4x4::perspective(_fovYRadians, safeAspect, _nearZ, _farZ);
    }

private:
    float _distanceZ;
    float _fovYRadians;
    float _nearZ;
    float _farZ;
};
