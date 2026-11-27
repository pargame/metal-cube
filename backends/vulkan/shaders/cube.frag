#version 450

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDirWorld;
} ubo;

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inAlbedo;

layout(location = 0) out vec4 outColor;

// [7단계: 광학적 상호작용 해석 (Optical Evaluation & Radiance Integration)]
void main() {
    vec3 N = normalize(inWorldNormal);
    vec3 L = normalize(ubo.lightDirWorld.xyz);

    float diffuseFactor = max(dot(N, L), 0.0);
    vec3 ambientRadiance = vec3(0.25);
    vec3 linearRadiance = inAlbedo * (ambientRadiance + diffuseFactor * 0.75);

    // [8단계] 별도 HDR 톤매핑 없이 Unorm 저장 시 하드웨어 포화(클램프)와 함께 sRGB 감마 인코딩 및 8비트 양자화
    outColor = vec4(linearRadiance, 1.0);
}
