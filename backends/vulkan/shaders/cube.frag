#version 450

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec3 inAlbedo;

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDirWorld;
} ubo;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(inWorldNormal);
    vec3 L = normalize(ubo.lightDirWorld.xyz);

    float diffuse = max(dot(N, L), 0.0);
    vec3 ambient = vec3(0.25);
    vec3 linearRadiance = inAlbedo * (ambient + diffuse * 0.75);

    outColor = vec4(linearRadiance, 1.0);
}
