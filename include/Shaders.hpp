#pragma once

#include "Math3D.hpp"

// CPU(시뮬레이션/관찰자 단계)에서 계산되어 GPU(셰이더 단계)로 전달되는 프레임 상태 구조체
// 기준 좌표계의 단계별 전이(Local -> World -> View -> Clip)를 셰이더 내부에서 명시적으로 수행하기 위해
// Model, View, Projection 행렬을 하나로 뭉개지 않고 각각 독립적으로 전달합니다.
struct FrameUniforms {
    Matrix4x4 modelMatrix;      // 2단계: 개체 고유 공간(Local) -> 세계 공간(World)
    Matrix4x4 viewMatrix;       // 3단계: 세계 공간(World) -> 관찰자 공간(View)
    Matrix4x4 projectionMatrix; // 4단계: 관찰자 공간(View) -> 클립/정규화 투영 공간(Clip/NDC)
    float lightDirWorld[4];     // 1단계 & 7단계: 세계 공간 기준 광원 방향 벡터
};

// Metal Shading Language (MSL) 소스 코드
// 정점 셰이더(vertexMain)는 기준 좌표계의 단계별 전이와 차원 축소(1~4단계)를 수행하고,
// 프래그먼트 셰이더(fragmentMain)는 확정된 가시 표면의 광학적 상호작용 해석(7단계)을 수행합니다.
inline const char* kShaderSource = R"(
    #include <metal_stdlib>
    using namespace metal;

    // [1단계: 개체 고유 공간(Local Space)의 입력 데이터]
    struct VertexInput {
        float3 localPosition [[attribute(0)]];
        float3 localNormal   [[attribute(1)]];
        float3 albedo        [[attribute(2)]];
    };

    // [4단계 -> 5단계 -> 7단계로 전달되는 정점 출력 및 보간 데이터]
    struct VertexOutput {
        float4 clipPosition [[position]]; // 4단계 사영 결과 (5단계 하드웨어 래스터라이저가 화면 좌표로 이산화)
        float3 worldNormal;               // 세계 공간 기준 표면 법선 벡터
        float3 albedo;                    // 표면 고유 반사 색상
    };

    struct FrameUniforms {
        float4x4 modelMatrix;
        float4x4 viewMatrix;
        float4x4 projectionMatrix;
        float4   lightDirWorld;
    };

    // [1단계 -> 2단계 -> 3단계 -> 4단계: 기준 좌표계 상대성 사슬 및 차원 축소]
    vertex VertexOutput vertexMain(VertexInput in [[stage_in]],
                                   constant FrameUniforms& uniforms [[buffer(1)]]) {
        VertexOutput out;

        // 1. 개체 고유 공간(Local Space) 위치 정의 (동차 좌표 w = 1.0)
        float4 localPos = float4(in.localPosition, 1.0);

        // 2. [2단계] 개체 고유 공간(Local) -> 세계 공간(World) 배치
        float4 worldPos = uniforms.modelMatrix * localPos;

        // 3. [3단계] 세계 공간(World) -> 관찰자 공간(View) 시점 변환
        float4 viewPos = uniforms.viewMatrix * worldPos;

        // 4. [4단계] 관찰자 공간(View, 3D) -> 클립/정규화 투영 공간(Clip/NDC, 2D + Depth) 차원 축소
        // 반환된 clipPosition은 GPU 고정 하드웨어에 의해 원근 분할(/w)되어 NDC 좌표로 변환됩니다.
        out.clipPosition = uniforms.projectionMatrix * viewPos;

        // 표면 법선 방향 벡터(동차 좌표 w = 0.0)를 세계 공간(World Space)으로 변환
        out.worldNormal = (uniforms.modelMatrix * float4(in.localNormal, 0.0)).xyz;
        out.albedo = in.albedo;

        return out;
    }

    // [7단계: 광학적 상호작용 해석 (Optical Evaluation & Radiance Integration)]
    // 5단계(공간 이산화)와 6단계(폐색 해결)를 통과하여 화면에 보이는 것으로 확정된 각 화소(Fragment)에 대해,
    // 광원에서 입사된 빛 에너지가 표면 물성(albedo) 및 기하 법선(worldNormal)과 반응하여
    // 관찰자 방향으로 반사되는 선형 복사휘도(Linear Radiance)를 산출합니다.
    fragment float4 fragmentMain(VertexOutput in [[stage_in]],
                                 constant FrameUniforms& uniforms [[buffer(1)]]) {
        float3 N = normalize(in.worldNormal);
        float3 L = normalize(uniforms.lightDirWorld.xyz);

        // 람베르트 코사인 법칙(Lambert's Cosine Law)에 따른 확산 반사(Diffuse) 조도 계산
        float diffuseFactor = max(dot(N, L), 0.0);

        // 간접광을 근사하는 환경광(Ambient)과 확산 반사광을 합산하여 최종 선형 빛 에너지 산출
        float3 ambientRadiance = float3(0.25);
        float3 linearRadiance = in.albedo * (ambientRadiance + diffuseFactor * 0.75);

        // 반환된 선형 색상 값은 8단계(인지적 색 공간 매핑 및 양자화)를 담당하는
        // sRGB 렌더 타겟 하드웨어에 의해 인간 시각 특성에 맞춘 비선형 8비트 값으로 변환됩니다.
        return float4(linearRadiance, 1.0);
    }
)";
