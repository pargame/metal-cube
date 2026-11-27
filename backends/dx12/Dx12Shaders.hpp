#pragma once

#include "core/FrameUniforms.hpp"

// HLSL 5.0 (vs_5_0 / ps_5_0) 소스 코드 — D3DCompile 런타임 컴파일 전용
// C++ Matrix4x4는 Column-Major(m[col * 4 + row])이며, HLSL cbuffer의 column_major float4x4도
// 4개의 열(Column) 벡터를 연속한 4개의 float4 레지스터로 적재합니다.
// 따라서 mul(matrix, vector)는 Metal(MSL)/GLSL의 (matrix * vector)와 수학적으로 100% 일치합니다.
inline constexpr const char* kDx12ShaderSource = R"(
    cbuffer FrameUniforms : register(b0) {
        column_major float4x4 modelMatrix;
        column_major float4x4 viewMatrix;
        column_major float4x4 projectionMatrix;
        float4                lightDirWorld;
    };

    struct VertexInput {
        float3 localPosition : POSITION;
        float3 localNormal   : NORMAL;
        float3 albedo        : COLOR;
    };

    struct VertexOutput {
        float4 clipPosition : SV_POSITION;
        float3 worldNormal  : NORMAL;
        float3 albedo       : COLOR;
    };

    // [1단계 -> 2단계 -> 3단계 -> 4단계: 기준 좌표계 상대성 사슬 및 사영 변환]
    VertexOutput vertexMain(VertexInput input) {
        VertexOutput output;

        float4 localPos = float4(input.localPosition, 1.0f);
        float4 worldPos = mul(modelMatrix, localPos);
        float4 viewPos  = mul(viewMatrix, worldPos);

        // [4단계] 관찰자 공간(View) -> 동차 절단 공간(Clip Space, D3D12 Z in [0, 1], +Y up)
        output.clipPosition = mul(projectionMatrix, viewPos);

        // 표면 법선 방향 벡터를 세계 공간으로 변환 (현재 모델은 순수 회전이므로 직교성 보존)
        output.worldNormal = mul(modelMatrix, float4(input.localNormal, 0.0f)).xyz;
        output.albedo = input.albedo;

        return output;
    }

    // [7단계: 광학적 상호작용 해석 (Optical Evaluation & Radiance Integration)]
    float4 pixelMain(VertexOutput input) : SV_TARGET {
        float3 N = normalize(input.worldNormal);
        float3 L = normalize(lightDirWorld.xyz);

        float diffuse = max(dot(N, L), 0.0f);
        float3 ambient = float3(0.25f, 0.25f, 0.25f);
        float3 linearRadiance = input.albedo * (ambient + diffuse * 0.75f);

        // 별도 HDR 톤매핑 없이 B8G8R8A8_UNORM_SRGB RTV 기록 시 하드웨어 포화(클램프)와 sRGB 감마 인코딩 수행
        return float4(linearRadiance, 1.0f);
    }
)";
