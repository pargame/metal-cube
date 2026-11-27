#pragma once

#include "core/FrameUniforms.hpp"

// Metal Shading Language (MSL) 소스 코드
// 정점 셰이더(vertexMain)는 기준 좌표계의 단계별 전이와 사영 변환(1~4단계)을 수행하고,
// 프래그먼트 셰이더(fragmentMain)는 확정된 가시 표면의 광학적 상호작용 해석(7단계)을 수행합니다.
inline constexpr const char* kMetalShaderSource = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VertexInput {
        float3 localPosition [[attribute(0)]];
        float3 localNormal   [[attribute(1)]];
        float3 albedo        [[attribute(2)]];
    };

    struct VertexOutput {
        float4 clipPosition [[position]];
        float3 worldNormal;
        float3 albedo;
    };

    struct FrameUniforms {
        float4x4 modelMatrix;
        float4x4 viewMatrix;
        float4x4 projectionMatrix;
        float4   lightDirWorld;
    };

    // [1단계 -> 2단계 -> 3단계 -> 4단계: 기준 좌표계 상대성 사슬 및 사영 변환]
    vertex VertexOutput vertexMain(VertexInput in [[stage_in]],
                                   constant FrameUniforms& uniforms [[buffer(1)]]) {
        VertexOutput out;

        float4 localPos = float4(in.localPosition, 1.0);
        float4 worldPos = uniforms.modelMatrix * localPos;
        float4 viewPos = uniforms.viewMatrix * worldPos;

        // [4단계] 관찰자 공간(View) -> 동차 절단 공간(Clip Space) 사영 변환
        out.clipPosition = uniforms.projectionMatrix * viewPos;

        // 표면 법선 방향 벡터를 세계 공간으로 변환 (현재 모델은 순수 회전이므로 직교성 보존)
        out.worldNormal = (uniforms.modelMatrix * float4(in.localNormal, 0.0)).xyz;
        out.albedo = in.albedo;

        return out;
    }

    // [7단계: 광학적 상호작용 해석 (Optical Evaluation & Radiance Integration)]
    fragment float4 fragmentMain(VertexOutput in [[stage_in]],
                                 constant FrameUniforms& uniforms [[buffer(1)]]) {
        float3 N = normalize(in.worldNormal);
        float3 L = normalize(uniforms.lightDirWorld.xyz);

        float diffuseFactor = max(dot(N, L), 0.0);
        float3 ambientRadiance = float3(0.25);
        float3 linearRadiance = in.albedo * (ambientRadiance + diffuseFactor * 0.75);

        // 별도 HDR 톤매핑 없이 Unorm 저장 시 하드웨어 포화(클램프)와 함께 sRGB 감마 인코딩 및 8비트 양자화
        return float4(linearRadiance, 1.0);
    }
)";
