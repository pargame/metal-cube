#pragma once

#include <array>
#include <type_traits>
#include "Math3D.hpp"

// [0.5단계: CPU↔GPU 데이터 ABI 계약]
// Metal(MSL), Vulkan(GLSL/SPIR-V), Direct3D 12(HLSL) 전 백엔드가 공유하는 프레임 상수 구조체.
// 16바이트 정렬 및 총 208바이트(DX12 CBV는 256바이트 정렬 버퍼에 배치)를 컴파일 타임 검증합니다.
struct alignas(16) FrameUniforms {
    Matrix4x4 modelMatrix;                // 2단계: Local -> World
    Matrix4x4 viewMatrix;                 // 3단계: World -> View
    Matrix4x4 projectionMatrix;           // 4단계: View -> Clip Space
    std::array<float, 4> lightDirWorld{}; // 1단계 & 7단계: 세계 공간 광원 방향 벡터
};

static_assert(std::is_standard_layout_v<FrameUniforms>, "FrameUniforms must maintain standard layout for GPU ABI.");
static_assert(sizeof(FrameUniforms) == 208, "FrameUniforms must be exactly 208 bytes (3 * 64 + 16).");
