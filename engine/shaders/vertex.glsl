#version 450 core

layout(location = 0) in uint aPackedVertex;

layout(std140, binding = 0) uniform WorldUBO
{
    mat4 MVP;
    vec4 light;       // xyz = position, w = radius
    vec4 ambientData; // x = ambient
};

uniform vec3 uChunkPosition;

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

    // 5 bits each.
    uint x =  data         & 0x1Fu;
    uint y = (data >> 5u)  & 0x1Fu;
    uint z = (data >> 10u) & 0x1Fu;

    // 3 bits.
    uint normal = (data >> 15u) & 0x07u;

    // 6 bits.
    uint texture = (data >> 18u) & 0x3Fu;

    // 2 bits.
    uint ao = (data >> 24u) & 0x03u;

    vec3 localPos = vec3(x, y, z);
    vec3 worldPos = localPos + uChunkPosition;

    gl_Position = MVP * vec4(worldPos, 1.0);

    vWorldPos = worldPos;
    vNormal = decodeNormal(normal);

    vTexIndex = texture;

    // 0..3 -> 0..1
    vAO = float(ao) / 3.0;

    uint corner = (data >> 26u) & 0x03u;

    const vec2 corners[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(1.0, 1.0),
        vec2(0.0, 1.0)
    );

    vUV = corners[corner];
}