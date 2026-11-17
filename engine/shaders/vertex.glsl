#version 450 core

layout(location = 0) in uint aPackedVertex;
// Per draw (instanced attribute, selected by baseInstance in multi-draw).
// Draws without the array enabled read the value set by glVertexAttrib3f.
layout(location = 1) in vec3 aChunkPosition;

layout(std140, binding = 0) uniform WorldUBO
{
    mat4 MVP;
    vec4 light;       // xyz = position, w = radius
    vec4 ambientData; // x = ambient
    vec4 eye;         // xyz = camera position
    vec4 fog;         // rgb = colour, w = distance (0 = off)
};

uniform bool uHighlightPass;
uniform vec3 uBoxSize;        // highlight pass: size of the unit-cube outline

out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
out float vAO;
flat out uint vTexIndex;

vec3 decodeNormal(uint normalIdx)
{
    if (normalIdx == 0u) return vec3( 1.0,  0.0,  0.0); // +X
    if (normalIdx == 1u) return vec3(-1.0,  0.0,  0.0); // -X
    if (normalIdx == 2u) return vec3( 0.0,  1.0,  0.0); // +Y
    if (normalIdx == 3u) return vec3( 0.0, -1.0,  0.0); // -Y
    if (normalIdx == 4u) return vec3( 0.0,  0.0,  1.0); // +Z
    if (normalIdx == 5u) return vec3( 0.0,  0.0, -1.0); // -Z

    return vec3(0.0, 1.0, 0.0);
}

void main()
{
    uint data = aPackedVertex;

    // X/Z use 5 bits; Y spans a complete 256-block chunk column.
    uint x =  data         & 0x1Fu;
    uint y = (data >> 5u)  & 0x1FFu;
    uint z = (data >> 14u) & 0x1Fu;

    // 3 bits.
    uint normal = (data >> 19u) & 0x07u;

    // 6 bits.
    uint texture = (data >> 22u) & 0x3Fu;

    // 2 bits.
    uint ao = (data >> 28u) & 0x03u;

    vec3 localPos = vec3(x, y, z);
    // Outline: scale the unit cube to the box, grown by 1% around its center
    // so it does not z-fight with the faces it surrounds.
    if (uHighlightPass)
        localPos = (localPos - vec3(0.5)) * uBoxSize * 1.01 + uBoxSize * 0.5;
    vec3 worldPos = localPos + aChunkPosition;

    gl_Position = MVP * vec4(worldPos, 1.0);

    vWorldPos = worldPos;
    vNormal = decodeNormal(normal);

    vTexIndex = texture;

    // Keep blocked corners shaded without making them completely black.
    vAO = 0.55 + (float(ao) / 3.0) * 0.45;

    if (normal <= 1u)
        vUV = vec2(localPos.z, 1.0 - localPos.y);
    else if (normal <= 3u)
        vUV = vec2(localPos.x, localPos.z);
    else
        vUV = vec2(localPos.x, 1.0 - localPos.y);
}
