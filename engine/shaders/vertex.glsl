#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in uint aTexIndex;
layout(location = 3) in int aNormal;  // Packed normal direction (3 bits)
layout(location = 4) in int aAO;      // Packed AO (0-255 -> 0.0-1.0)

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

// Decode packed normal direction (0-5) to actual normal vector
vec3 decodeNormal(int normalIdx) {
    if (normalIdx == 0) return vec3(1, 0, 0);   // +X
    if (normalIdx == 1) return vec3(-1, 0, 0);  // -X
    if (normalIdx == 2) return vec3(0, 1, 0);   // +Y
    if (normalIdx == 3) return vec3(0, -1, 0);  // -Y
    if (normalIdx == 4) return vec3(0, 0, 1);   // +Z
    if (normalIdx == 5) return vec3(0, 0, -1);  // -Z
    return vec3(0, 1, 0);  // Fallback
}

// Decode packed AO (0-255) back to float (0.0-1.0)
float decodeAO(int ao) {
    return float(ao) / 255.0;
}

void main()
{

    gl_Position = MVP * vec4(aPos, 1.0);
    vUV = aUV;
    vWorldPos = aPos;
    vTexIndex = aTexIndex;

    int normalIdx = aNormal & 7;
    vNormal = decodeNormal(normalIdx);

    int ao = aAO & 255;
    vAO = decodeAO(ao);
}
