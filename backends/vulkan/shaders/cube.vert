#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDirWorld;
} ubo;

layout(location = 0) out vec3 outWorldNormal;
layout(location = 1) out vec3 outAlbedo;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPos, 1.0);
    outWorldNormal = (ubo.model * vec4(inNormal, 0.0)).xyz;
    outAlbedo = inColor;
}
