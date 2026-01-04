#version 450 core
layout(early_fragment_tests) in;

in vec2 vUV;
in vec3 vNormal;
in vec3 vWorldPos;
in float vAO;
flat in uint vTexIndex;

layout(binding = 0) uniform sampler2DArray uTextures;
layout(std140, binding = 0) uniform WorldUBO
{
    mat4 MVP;
    vec4 light;
    vec4 ambientData;
};

out vec4 FragColor;

void main()
{
    vec2 uv = clamp(vUV, 0.001, 0.999);
    vec4 tex = texture(uTextures, vec3(uv, vTexIndex));
    if (tex.a < 0.1) discard;

    float dist = distance(vWorldPos, light.xyz);
    float lightFactor = clamp(1.0 - dist / light.w, 0.0, 1.0);

    float ambient = ambientData.x;
    float brightness = (ambient + lightFactor) * vAO * 1.25;

    FragColor = vec4(tex.rgb * brightness, tex.a);

}
