#version 450 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in uint  aTexIndex;
layout(location = 3) in uint  aNormal;
layout(location = 4) in uint  aAO;

layout(std140, binding = 0) uniform WorldUBO
{
    mat4 MVP;
    vec4 light;
    vec4 cameraPos;
};

const vec3 normals[6] = vec3[](
    vec3( 1,  0,  0),  // +X
    vec3(-1,  0,  0),  // -X
    vec3( 0,  1,  0),  // +Y
    vec3( 0, -1,  0),  // -Y
    vec3( 0,  0,  1),  // +Z
    vec3( 0,  0, -1)   // -Z
);

out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
out float vAO;
flat out uint vTexIndex;

void main()
{
    gl_Position = MVP * vec4(aPos, 1.0);
    vUV = vec2(aUV);
    vNormal = normals[aNormal];
    vWorldPos = aPos;
    vTexIndex = aTexIndex;
    vAO = float(aAO) / 3.0;
}
