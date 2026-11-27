#pragma once

#include <numbers>
#include "Math3D.hpp"

// [3단계: 관찰자의 정립과 시점 변환] & [4단계: 원근 사영 변환 규격 정의]
class Observer {
public:
    constexpr Observer() noexcept
        : _distanceZ(-3.2f),
          _fovYRadians(60.0f * (std::numbers::pi_v<float> / 180.0f)),
          _nearZ(0.1f),
          _farZ(100.0f) {}

    constexpr Matrix4x4 computeViewMatrix() const noexcept {
        return Matrix4x4::translation(0.0f, 0.0f, _distanceZ);
    }

    // [4단계: 관찰자 공간(View Space) -> 동차 절단 공간(Clip Space) 원근 사영 변환]
    // flipY=true는 Vulkan의 +Y down NDC 규약을 지원합니다.
    Matrix4x4 computeProjectionMatrix(float aspectRatio, bool flipY = false) const noexcept {
        float safeAspect = (aspectRatio > 0.0f) ? aspectRatio : 1.0f;
        return Matrix4x4::perspective(_fovYRadians, safeAspect, _nearZ, _farZ, flipY);
    }

private:
    float _distanceZ;
    float _fovYRadians;
    float _nearZ;
    float _farZ;
};
