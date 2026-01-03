#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uint aTexIndex;
layout(location = 4) in float aAO;

layout(std140, binding = 0) uniform WorldUBO
{
    mat4 MVP;
    vec4 light;       // xyz = pos, w = radius
    vec4 ambientData; // x = ambient, yzw = padding
};

out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
out float vAO;
flat out uint vTexIndex;

void main()
{
    gl_Position = MVP * vec4(aPos, 1.0);
    vUV = aUV;
    vNormal = aNormal;
    vWorldPos = aPos;
    vTexIndex = aTexIndex;
    vAO = aAO;
}
