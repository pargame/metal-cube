#version 450

// [1단계: 개체 고유 공간(Local Space) 정점 입력]
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

// [0.5단계: CPU↔GPU 데이터 ABI — FrameUniforms (208바이트)]
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDirWorld;
} ubo;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 outAlbedo;

// [1단계 -> 2단계 -> 3단계 -> 4단계: 기준 좌표계 상대성 사슬 및 사영 변환]
void main() {
    // 1. 개체 고유 공간(Local Space) 동차 좌표
    vec4 localPos = vec4(inPos, 1.0);
    // 2. [2단계] 개체 고유 공간(Local) -> 세계 공간(World) 배치
    vec4 worldPos = ubo.model * localPos;
    // 3. [3단계] 세계 공간(World) -> 관찰자 공간(View) 시점 변환
    vec4 viewPos = ubo.view * worldPos;
    // 4. [4단계] 관찰자 공간(View) -> 동차 절단 공간(Clip Space) 사영 변환
    gl_Position = ubo.proj * viewPos;

    // 표면 법선 방향 벡터를 세계 공간으로 변환 (현재 모델은 순수 회전이므로 직교성 보존)
    outWorldNormal = (ubo.model * vec4(inNormal, 0.0)).xyz;
    outAlbedo = inColor;
}
